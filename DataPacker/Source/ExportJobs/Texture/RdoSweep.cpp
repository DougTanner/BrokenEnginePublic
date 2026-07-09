#include "RdoSweep.h"

#include "Texture.h"

struct RdoSweepResult
{
	uint32_t uiLookback;
	float fLambda;
	int iUberLevel;
	double fEncodeSeconds;
	int64_t iRawBc7Bytes;
	int64_t iDeflatedBytes;
	double fDeflatePercentSaved;
};

static RdoSweepResult RunRdoSweepOne(const std::vector<float>& rPixels, int64_t iWidth, int64_t iHeight, uint32_t uiLookback, float fLambda, int iUberLevel)
{
	int64_t iBc7Size = ((iWidth + 3) / 4) * ((iHeight + 3) / 4) * 16;
	std::vector<std::byte> bc7Output(iBc7Size);

	auto tStart = std::chrono::steady_clock::now();
	Texture::EncodeWithRdo(bc7Output.data(), rPixels, iWidth, iHeight, VK_FORMAT_BC7_UNORM_BLOCK, fLambda, uiLookback, iUberLevel, {});
	auto tEnd = std::chrono::steady_clock::now();
	double fEncodeSeconds = std::chrono::duration<double>(tEnd - tStart).count();

	int64_t iDeflatedSize = static_cast<int64_t>(ZlibCompress(bc7Output.data(), iBc7Size).size());

	return RdoSweepResult{
		.uiLookback = uiLookback,
		.fLambda = fLambda,
		.iUberLevel = iUberLevel,
		.fEncodeSeconds = fEncodeSeconds,
		.iRawBc7Bytes = iBc7Size,
		.iDeflatedBytes = iDeflatedSize,
		.fDeflatePercentSaved = 100.0 * (1.0 - static_cast<double>(iDeflatedSize) / static_cast<double>(iBc7Size)),
	};
}

int RunRdoSweep(const std::filesystem::path& rPath)
{
	LOG(kDefault, kInfo, "RDO sweep input: \"{}\"", rPath.string());

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(rPath, FileType::kImage);
	int64_t iWidth = texture.miWidth;
	int64_t iHeight = texture.miHeight;
	LOG(kDefault, kInfo, "Source dimensions: {}x{}", iWidth, iHeight);
	const std::vector<float>& rMip0 = texture.mData.at(0);

	auto logResult = [](const RdoSweepResult& r)
	{
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} uber={} encode={:6.2f}s raw={:9} deflated={:9} saved={:5.1f}%", r.uiLookback, r.fLambda, r.iUberLevel, r.fEncodeSeconds, r.iRawBc7Bytes, r.iDeflatedBytes, r.fDeflatePercentSaved);
	};

	LOG(kDefault, kInfo, "--- Lookback sweep (lambda=0.5, uber=2) ---");
	for (uint32_t uiLookback : {1024u, 2048u, 4096u, 8192u, 16384u, 32768u})
	{
		logResult(RunRdoSweepOne(rMip0, iWidth, iHeight, uiLookback, 0.5f, 2));
	}

	LOG(kDefault, kInfo, "--- Lambda sweep (lookback=4096, uber=2; lambda=0 is non-RDO baseline) ---");
	for (float fLambda : {0.0f, 0.25f, 1.0f, 2.0f})
	{
		logResult(RunRdoSweepOne(rMip0, iWidth, iHeight, 4096u, fLambda, 2));
	}

	LOG(kDefault, kInfo, "--- Uber sweep (lookback=4096, lambda=0.5) ---");
	for (int iUberLevel : {4, 6})
	{
		logResult(RunRdoSweepOne(rMip0, iWidth, iHeight, 4096u, 0.5f, iUberLevel));
	}

	return 0;
}

int RunRdoSweepFull(const std::filesystem::path& rPath)
{
	LOG(kDefault, kInfo, "RDO full-grid sweep input: \"{}\"", rPath.string());

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(rPath, FileType::kImage);
	int64_t iWidth = texture.miWidth;
	int64_t iHeight = texture.miHeight;
	LOG(kDefault, kInfo, "Source dimensions: {}x{}", iWidth, iHeight);
	const std::vector<float>& rMip0 = texture.mData.at(0);

	// Uber fixed at 4: prior OAT showed uber=2 is strictly worse and uber=6 is identical to uber=4.
	static constexpr int kiUberLevel = 4;
	static const std::array<uint32_t, 5> kLookbacks {1024u, 2048u, 4096u, 8192u, 16384u};
	static const std::array<float, 6> kLambdas {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

	std::vector<RdoSweepResult> results;
	results.reserve(kLookbacks.size() * kLambdas.size());

	auto tGridStart = std::chrono::steady_clock::now();
	for (uint32_t uiLookback : kLookbacks)
	{
		for (float fLambda : kLambdas)
		{
			results.push_back(RunRdoSweepOne(rMip0, iWidth, iHeight, uiLookback, fLambda, kiUberLevel));
			const RdoSweepResult& r = results.back();
			LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} encode={:6.2f}s deflated={:9} saved={:5.1f}%", r.uiLookback, r.fLambda, r.fEncodeSeconds, r.iDeflatedBytes, r.fDeflatePercentSaved);
		}
	}
	auto tGridEnd = std::chrono::steady_clock::now();
	double fTotalSeconds = std::chrono::duration<double>(tGridEnd - tGridStart).count();
	LOG(kDefault, kInfo, "");
	LOG(kDefault, kInfo, "Grid complete: {} encodes in {:.1f}s wall-clock (uber={})", static_cast<int64_t>(results.size()), fTotalSeconds, kiUberLevel);

	// Pareto frontier: keep configs where no other config has BOTH less time AND less-or-equal size (or vice versa).
	std::vector<const RdoSweepResult*> pareto;
	for (const RdoSweepResult& r : results)
	{
		bool bDominated = false;
		for (const RdoSweepResult& rOther : results)
		{
			if (&rOther == &r)
			{
				continue;
			}
			bool bOtherFaster = rOther.fEncodeSeconds < r.fEncodeSeconds;
			bool bOtherSmaller = rOther.iDeflatedBytes < r.iDeflatedBytes;
			bool bOtherFasterOrEqual = rOther.fEncodeSeconds <= r.fEncodeSeconds;
			bool bOtherSmallerOrEqual = rOther.iDeflatedBytes <= r.iDeflatedBytes;
			if ((bOtherFaster && bOtherSmallerOrEqual) || (bOtherFasterOrEqual && bOtherSmaller))
			{
				bDominated = true;
				break;
			}
		}
		if (!bDominated)
		{
			pareto.push_back(&r);
		}
	}

	std::sort(pareto.begin(), pareto.end(), [](const RdoSweepResult* pA, const RdoSweepResult* pB) { return pA->fEncodeSeconds < pB->fEncodeSeconds; });

	LOG(kDefault, kInfo, "");
	LOG(kDefault, kInfo, "Pareto frontier (faster-and-smaller dominators removed):");
	for (const RdoSweepResult* pResult : pareto)
	{
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} encode={:6.2f}s deflated={:9} saved={:5.1f}%", pResult->uiLookback, pResult->fLambda, pResult->fEncodeSeconds, pResult->iDeflatedBytes, pResult->fDeflatePercentSaved);
	}

	return 0;
}

// Decode a Texture::Save'd BC7 intermediate back to RGBA8 at mip 0, returning the
// 4-floats-per-pixel buffer EncodeWithRdo expects. Accepts both the magic-prefixed
// current format and pre-magic legacy zlib files (so the validate flag works whether
// the user migrated first or not).
static std::vector<float> LoadBc7AsFloatPixelsMip0(const std::filesystem::path& rPath, int64_t& riWidth, int64_t& riHeight)
{
	std::vector<std::byte> fileBytes = common::ReadEntireFile(rPath);
	TextureIntermediateHeader header = ReadTextureIntermediateHeader(fileBytes.data(), static_cast<int64_t>(fileBytes.size()));
	riWidth = header.iWidth;
	riHeight = header.iHeight;
	int64_t iMipMaps = header.iMipCount;
	ASSERT(riWidth > 0 && riHeight > 0 && iMipMaps > 0);

	int64_t iCompressedSize = static_cast<int64_t>(fileBytes.size()) - header.iPayloadOffset;
	ASSERT(iCompressedSize > 0);
	std::vector<std::byte> compressed(fileBytes.begin() + header.iPayloadOffset, fileBytes.end());

	int64_t iAllMipsSize = 0;
	int64_t iMipWidth = riWidth;
	int64_t iMipHeight = riHeight;
	for (int64_t i = 0; i < iMipMaps; ++i)
	{
		iAllMipsSize += ((iMipWidth + 3) / 4) * ((iMipHeight + 3) / 4) * 16;
		iMipWidth /= 2;
		iMipHeight /= 2;
	}

	std::vector<std::byte> bc7Bytes(iAllMipsSize);
	uLongf uiUncompressedSize = static_cast<uLongf>(iAllMipsSize);
	int iZlibResult = uncompress(reinterpret_cast<Bytef*>(bc7Bytes.data()), &uiUncompressedSize, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(iCompressedSize));
	ASSERT(iZlibResult == Z_OK);

	int64_t iBlocksX = (riWidth + 3) / 4;
	int64_t iBlocksY = (riHeight + 3) / 4;
	std::vector<float> pixels(static_cast<size_t>(riWidth * riHeight * 4));

	for (int64_t iBlockY = 0; iBlockY < iBlocksY; ++iBlockY)
	{
		for (int64_t iBlockX = 0; iBlockX < iBlocksX; ++iBlockX)
		{
			const std::byte* pBlock = bc7Bytes.data() + (iBlockY * iBlocksX + iBlockX) * 16;
			bc7decomp::color_rgba decoded[16];
			bc7decomp::unpack_bc7(pBlock, decoded);

			for (int64_t iLocal = 0; iLocal < 16; ++iLocal)
			{
				int64_t iX = iBlockX * 4 + (iLocal % 4);
				int64_t iY = iBlockY * 4 + (iLocal / 4);
				if (iX >= riWidth || iY >= riHeight)
				{
					continue;
				}
				int64_t iPixelIndex = (iY * riWidth + iX) * 4;
				pixels.at(iPixelIndex + 0) = static_cast<float>(decoded[iLocal].r);
				pixels.at(iPixelIndex + 1) = static_cast<float>(decoded[iLocal].g);
				pixels.at(iPixelIndex + 2) = static_cast<float>(decoded[iLocal].b);
				pixels.at(iPixelIndex + 3) = static_cast<float>(decoded[iLocal].a);
			}
		}
	}

	return pixels;
}

int RunRdoSweepValidate(const std::filesystem::path& rPath)
{
	LOG(kDefault, kInfo, "RDO validate input: \"{}\"", rPath.string());

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	int64_t iWidth = 0;
	int64_t iHeight = 0;
	std::vector<float> pixels = LoadBc7AsFloatPixelsMip0(rPath, iWidth, iHeight);
	LOG(kDefault, kInfo, "Decoded mip 0: {}x{}", iWidth, iHeight);

	// Validate the recommended Pareto-knee config and the next two corners on the frontier.
	struct ConfigToTest { uint32_t uiLookback; float fLambda; int iUberLevel; };
	static const std::array<ConfigToTest, 4> kConfigs {{
		{1024u, 4.0f, 4},
		{2048u, 4.0f, 4},
		{4096u, 4.0f, 4},
		{4096u, 0.5f, 2},   // former production knobs, for direct comparison (current knobs: constants atop ExportJobs/Texture/Texture.cpp)
	}};

	for (const ConfigToTest& rConfig : kConfigs)
	{
		RdoSweepResult result = RunRdoSweepOne(pixels, iWidth, iHeight, rConfig.uiLookback, rConfig.fLambda, rConfig.iUberLevel);
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} uber={} encode={:7.2f}s deflated={:10} saved={:5.1f}%", result.uiLookback, result.fLambda, result.iUberLevel, result.fEncodeSeconds, result.iDeflatedBytes, result.fDeflatePercentSaved);
	}

	return 0;
}
