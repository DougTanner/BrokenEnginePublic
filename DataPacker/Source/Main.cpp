#include "FileManager.h"

#include "Attribution.h"
#include "BakeIslandIntermediates.h"
#include "Texture.h"
#include "ExportJobs/ExportAudio.h"
#include "ExportJobs/ExportFont.h"
#include "ExportJobs/ExportScene.h"
#include "ExportJobs/ExportIsland.h"
#include "ExportJobs/ExportModel.h"
#include "ExportJobs/ExportRaw.h"
#include "ExportJobs/ExportShader.h"
#include "ExportJobs/ExportTexture.h"

#include "bc7enc_rdo/bc7decomp.h"
#include "bc7enc_rdo/rgbcx.h"

using enum common::ChunkFlags;

void Quit(const char* message, const char* title);

static void WriteIfChanged(const std::string& rContent, const std::filesystem::path& rPath, const char* logName)
{
	if (!common::ContentsEqual(rContent, rPath))
	{
		std::fstream stream(rPath, std::ios::out | std::ios::binary);
		stream << rContent;
		stream.close();
		LOG(kDefault, kDebug, "Re-generated {}", logName);
	}
}

static VkFormat IntermediateFormatFromExtension(const std::filesystem::path& rPath)
{
	const std::string sExtension = rPath.extension().string();
	if (sExtension == ".BC4_UNORM_BLOCK") return VK_FORMAT_BC4_UNORM_BLOCK;
	if (sExtension == ".BC5_UNORM_BLOCK") return VK_FORMAT_BC5_UNORM_BLOCK;
	if (sExtension == ".BC7_UNORM_BLOCK") return VK_FORMAT_BC7_UNORM_BLOCK;
	if (sExtension == ".R16_UNORM") return VK_FORMAT_R16_UNORM;
	return VK_FORMAT_UNDEFINED;
}

// Decode a single mip's BCn block stream (raw, no header) to an RGBA8 byte buffer.
// BC4 fills R only (G=B=0, A=255). BC5 fills R+G (B=0, A=255). BC7 fills RGBA verbatim.
static std::vector<uint8_t> DecodeBcnMip0ToRgba8(const std::byte* puiEncoded, int64_t iWidth, int64_t iHeight, VkFormat vkFormat)
{
	int64_t iBlocksX = (iWidth + 3) / 4;
	int64_t iBlocksY = (iHeight + 3) / 4;
	int64_t iBlockSizeBytes = (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK) ? 8 : 16;
	std::vector<uint8_t> rgba8(static_cast<size_t>(iWidth * iHeight * 4));

	for (int64_t iBlockY = 0; iBlockY < iBlocksY; ++iBlockY)
	{
		for (int64_t iBlockX = 0; iBlockX < iBlocksX; ++iBlockX)
		{
			const std::byte* pBlock = puiEncoded + (iBlockY * iBlocksX + iBlockX) * iBlockSizeBytes;
			std::array<uint8_t, 16 * 4> blockRgba {};

			switch (vkFormat)
			{
				case VK_FORMAT_BC4_UNORM_BLOCK:
				{
					std::array<uint8_t, 16> bc4Pixels {};
					rgbcx::unpack_bc4(pBlock, bc4Pixels.data(), 1);
					for (int i = 0; i < 16; ++i)
					{
						blockRgba.at(i * 4 + 0) = bc4Pixels.at(i);
						blockRgba.at(i * 4 + 3) = 255;
					}
					break;
				}
				case VK_FORMAT_BC5_UNORM_BLOCK:
				{
					std::array<uint8_t, 16 * 2> bc5Pixels {};
					rgbcx::unpack_bc5(pBlock, bc5Pixels.data(), 0, 1, 2);
					for (int i = 0; i < 16; ++i)
					{
						blockRgba.at(i * 4 + 0) = bc5Pixels.at(i * 2 + 0);
						blockRgba.at(i * 4 + 1) = bc5Pixels.at(i * 2 + 1);
						blockRgba.at(i * 4 + 3) = 255;
					}
					break;
				}
				case VK_FORMAT_BC7_UNORM_BLOCK:
				{
					std::array<bc7decomp::color_rgba, 16> bc7Pixels {};
					bc7decomp::unpack_bc7(pBlock, bc7Pixels.data());
					for (int i = 0; i < 16; ++i)
					{
						blockRgba.at(i * 4 + 0) = bc7Pixels.at(i).r;
						blockRgba.at(i * 4 + 1) = bc7Pixels.at(i).g;
						blockRgba.at(i * 4 + 2) = bc7Pixels.at(i).b;
						blockRgba.at(i * 4 + 3) = bc7Pixels.at(i).a;
					}
					break;
				}
				default:
					ASSERT(false);
					break;
			}

			for (int64_t by = 0; by < 4; ++by)
			{
				for (int64_t bx = 0; bx < 4; ++bx)
				{
					int64_t iX = iBlockX * 4 + bx;
					int64_t iY = iBlockY * 4 + by;
					if (iX >= iWidth || iY >= iHeight)
					{
						continue;
					}
					int64_t iDestIndex = (iY * iWidth + iX) * 4;
					int64_t iBlockIndex = (by * 4 + bx) * 4;
					rgba8.at(iDestIndex + 0) = blockRgba.at(iBlockIndex + 0);
					rgba8.at(iDestIndex + 1) = blockRgba.at(iBlockIndex + 1);
					rgba8.at(iDestIndex + 2) = blockRgba.at(iBlockIndex + 2);
					rgba8.at(iDestIndex + 3) = blockRgba.at(iBlockIndex + 3);
				}
			}
		}
	}

	return rgba8;
}

// Idempotent migration: skips files already prefixed with kiTextureIntermediateMagic.
// For legacy raw BCn files: decodes mip 0, re-encodes with current RDO knobs (Texture::Save
// regenerates the mip chain and writes the new format with magic). For legacy raw R16 files:
// just zlib-wraps with magic — R16 has no encoding step.
static void MigrateLegacyIntermediate(const std::filesystem::path& rPath)
{
	VkFormat vkFormat = IntermediateFormatFromExtension(rPath);
	if (vkFormat == VK_FORMAT_UNDEFINED)
	{
		return;
	}

	int64_t iFileSize = std::filesystem::file_size(rPath);
	int64_t iLegacyHeaderSize = 3 * static_cast<int64_t>(sizeof(int64_t));
	if (iFileSize < iLegacyHeaderSize + static_cast<int64_t>(sizeof(int64_t)))
	{
		return;
	}

	std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
	int64_t iFirstQword = 0;
	fileStream.read(reinterpret_cast<char*>(&iFirstQword), sizeof(iFirstQword));
	if (iFirstQword == kiTextureIntermediateMagic)
	{
		return;
	}

	auto tMigrateStart = std::chrono::steady_clock::now();
	int64_t iSizeBefore = iFileSize;

	int64_t iWidth = iFirstQword;
	int64_t iHeight = 0;
	int64_t iMipMaps = 0;
	fileStream.read(reinterpret_cast<char*>(&iHeight), sizeof(iHeight));
	fileStream.read(reinterpret_cast<char*>(&iMipMaps), sizeof(iMipMaps));

	if (iWidth <= 0 || iHeight <= 0 || iMipMaps <= 0 || iWidth > 32768 || iHeight > 32768 || iMipMaps > 32)
	{
		return;
	}

	int64_t iExpectedRawSize = 0;
	int64_t iMipWidth = iWidth;
	int64_t iMipHeight = iHeight;
	for (int64_t i = 0; i < iMipMaps; ++i)
	{
		iExpectedRawSize += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth /= 2;
		iMipHeight /= 2;
	}

	int64_t iPayloadSize = iFileSize - iLegacyHeaderSize;
	std::vector<std::byte> payload(iPayloadSize);
	fileStream.read(reinterpret_cast<char*>(payload.data()), iPayloadSize);
	fileStream.close();

	// Two valid legacy shapes for the payload:
	//   1. Pre-zlib raw bytes (size == iExpectedRawSize)
	//   2. Zlib-wrapped without magic (size != raw, but decompresses to iExpectedRawSize)
	// The second case happens for files migrated by an earlier session before this magic
	// scheme was added. Both flow through the same decode + re-encode below.
	std::vector<std::byte> rawBytes;
	if (iPayloadSize == iExpectedRawSize)
	{
		rawBytes = std::move(payload);
	}
	else
	{
		rawBytes.resize(iExpectedRawSize);
		uLongf uiUncompressedSize = static_cast<uLongf>(iExpectedRawSize);
		int iZlibResult = uncompress(reinterpret_cast<Bytef*>(rawBytes.data()), &uiUncompressedSize,
		                             reinterpret_cast<const Bytef*>(payload.data()), static_cast<uLong>(iPayloadSize));
		if (iZlibResult != Z_OK || static_cast<int64_t>(uiUncompressedSize) != iExpectedRawSize)
		{
			return;
		}
	}

	if (vkFormat == VK_FORMAT_R16_UNORM)
	{
		// R16 has no encoding step. Compress the raw bytes and rewrite with magic.
		uLongf uiCompressedBound = compressBound(static_cast<uLong>(iExpectedRawSize));
		std::vector<std::byte> compressed(uiCompressedBound);
		uLongf uiCompressedSize = uiCompressedBound;
		int iZlibResult = compress2(reinterpret_cast<Bytef*>(compressed.data()), &uiCompressedSize,
		                            reinterpret_cast<const Bytef*>(rawBytes.data()), static_cast<uLong>(iExpectedRawSize),
		                            Z_BEST_COMPRESSION);
		ASSERT(iZlibResult == Z_OK);
		compressed.resize(uiCompressedSize);

		std::filesystem::remove(rPath);
		std::fstream fileStreamOut(rPath, std::ios::out | std::ios::binary);
		int64_t iMagic = kiTextureIntermediateMagic;
		fileStreamOut.write(reinterpret_cast<const char*>(&iMagic), sizeof(iMagic));
		fileStreamOut.write(reinterpret_cast<const char*>(&iWidth), sizeof(iWidth));
		fileStreamOut.write(reinterpret_cast<const char*>(&iHeight), sizeof(iHeight));
		fileStreamOut.write(reinterpret_cast<const char*>(&iMipMaps), sizeof(iMipMaps));
		fileStreamOut.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
		fileStreamOut.flush();
		fileStreamOut.close();

		auto tMigrateEnd = std::chrono::steady_clock::now();
		double fMigrateSeconds = std::chrono::duration<double>(tMigrateEnd - tMigrateStart).count();
		int64_t iSizeAfter = std::filesystem::file_size(rPath);
		LOG(kDefault, kInfo, "Migrated R16 (zlib + magic): \"{}\" ({} -> {} bytes raw; {} -> {} bytes on-disk; {:.2f}s)",
			rPath.string(), iExpectedRawSize, static_cast<int64_t>(uiCompressedSize),
			iSizeBefore, iSizeAfter, fMigrateSeconds);
		return;
	}

	std::vector<uint8_t> rgba8 = DecodeBcnMip0ToRgba8(rawBytes.data(), iWidth, iHeight, vkFormat);

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(reinterpret_cast<const std::byte*>(rgba8.data()), iWidth, iHeight, 4);
		texture.MakeMipmaps(vkFormat);
		texture.Save(rPath, vkFormat, false);
	}

	auto tMigrateEnd = std::chrono::steady_clock::now();
	double fMigrateSeconds = std::chrono::duration<double>(tMigrateEnd - tMigrateStart).count();
	int64_t iSizeAfter = std::filesystem::file_size(rPath);
	LOG(kDefault, kInfo, "Migrated BCn (decode + re-encode with current knobs + magic): \"{}\" ({}x{}; {} -> {} bytes on-disk; {:.2f}s)",
		rPath.string(), iWidth, iHeight, iSizeBefore, iSizeAfter, fMigrateSeconds);
}

static void MigrateLegacyIntermediates()
{
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			if (rEntry.is_regular_file())
			{
				MigrateLegacyIntermediate(rEntry.path());
			}
		}
	}
}

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
	Texture::EncodeWithRdo(bc7Output.data(), rPixels, iWidth, iHeight, VK_FORMAT_BC7_UNORM_BLOCK, fLambda, uiLookback, iUberLevel, false);
	auto tEnd = std::chrono::steady_clock::now();
	double fEncodeSeconds = std::chrono::duration<double>(tEnd - tStart).count();

	uLongf uiCompressedBound = compressBound(static_cast<uLong>(iBc7Size));
	std::vector<std::byte> deflated(uiCompressedBound);
	uLongf uiDeflatedSize = uiCompressedBound;
	int iZlibResult = compress2(reinterpret_cast<Bytef*>(deflated.data()), &uiDeflatedSize,
	                            reinterpret_cast<const Bytef*>(bc7Output.data()), static_cast<uLong>(iBc7Size),
	                            Z_BEST_COMPRESSION);
	ASSERT(iZlibResult == Z_OK);

	return RdoSweepResult{
		.uiLookback = uiLookback,
		.fLambda = fLambda,
		.iUberLevel = iUberLevel,
		.fEncodeSeconds = fEncodeSeconds,
		.iRawBc7Bytes = iBc7Size,
		.iDeflatedBytes = static_cast<int64_t>(uiDeflatedSize),
		.fDeflatePercentSaved = 100.0 * (1.0 - static_cast<double>(uiDeflatedSize) / static_cast<double>(iBc7Size)),
	};
}

static int RunRdoSweep(const std::filesystem::path& rPath)
{
	LOG(kDefault, kInfo, "RDO sweep input: \"{}\"", rPath.string());

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(rPath, FileType::kImage, false);
	int64_t iWidth = texture.miWidth;
	int64_t iHeight = texture.miHeight;
	LOG(kDefault, kInfo, "Source dimensions: {}x{}", iWidth, iHeight);
	const std::vector<float>& rMip0 = texture.mData.at(0);

	auto logResult = [](const RdoSweepResult& r)
	{
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} uber={} encode={:6.2f}s raw={:9} deflated={:9} saved={:5.1f}%",
			r.uiLookback, r.fLambda, r.iUberLevel, r.fEncodeSeconds, r.iRawBc7Bytes, r.iDeflatedBytes, r.fDeflatePercentSaved);
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

static int RunRdoSweepFull(const std::filesystem::path& rPath)
{
	LOG(kDefault, kInfo, "RDO full-grid sweep input: \"{}\"", rPath.string());

	std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
	Texture texture(rPath, FileType::kImage, false);
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
			LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} encode={:6.2f}s deflated={:9} saved={:5.1f}%",
				r.uiLookback, r.fLambda, r.fEncodeSeconds, r.iDeflatedBytes, r.fDeflatePercentSaved);
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
			if (&rOther == &r) continue;
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

	std::sort(pareto.begin(), pareto.end(),
		[](const RdoSweepResult* pA, const RdoSweepResult* pB) { return pA->fEncodeSeconds < pB->fEncodeSeconds; });

	LOG(kDefault, kInfo, "");
	LOG(kDefault, kInfo, "Pareto frontier (faster-and-smaller dominators removed):");
	for (const RdoSweepResult* pResult : pareto)
	{
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} encode={:6.2f}s deflated={:9} saved={:5.1f}%",
			pResult->uiLookback, pResult->fLambda, pResult->fEncodeSeconds, pResult->iDeflatedBytes, pResult->fDeflatePercentSaved);
	}

	return 0;
}

// Decode a Texture::Save'd BC7 intermediate back to RGBA8 at mip 0, returning the
// 4-floats-per-pixel buffer EncodeWithRdo expects. Accepts both the magic-prefixed
// current format and pre-magic legacy zlib files (so the validate flag works whether
// the user migrated first or not).
static std::vector<float> LoadBc7AsFloatPixelsMip0(const std::filesystem::path& rPath, int64_t& riWidth, int64_t& riHeight)
{
	std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
	int64_t iFileSize = std::filesystem::file_size(rPath);
	int64_t iMipMaps = 0;
	int64_t iFirstQword = 0;
	fileStream.read(reinterpret_cast<char*>(&iFirstQword), sizeof(iFirstQword));
	int64_t iHeaderSize = 0;
	if (iFirstQword == kiTextureIntermediateMagic)
	{
		fileStream.read(reinterpret_cast<char*>(&riWidth), sizeof(riWidth));
		iHeaderSize = 4 * static_cast<int64_t>(sizeof(int64_t));
	}
	else
	{
		riWidth = iFirstQword;
		iHeaderSize = 3 * static_cast<int64_t>(sizeof(int64_t));
	}
	fileStream.read(reinterpret_cast<char*>(&riHeight), sizeof(riHeight));
	fileStream.read(reinterpret_cast<char*>(&iMipMaps), sizeof(iMipMaps));

	int64_t iCompressedSize = iFileSize - iHeaderSize;
	std::vector<std::byte> compressed(iCompressedSize);
	fileStream.read(reinterpret_cast<char*>(compressed.data()), iCompressedSize);
	fileStream.close();

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
	int iZlibResult = uncompress(reinterpret_cast<Bytef*>(bc7Bytes.data()), &uiUncompressedSize,
	                             reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(iCompressedSize));
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

static int RunRdoSweepValidate(const std::filesystem::path& rPath)
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
		{4096u, 0.5f, 2},   // current production knobs, for direct comparison
	}};

	for (const ConfigToTest& rConfig : kConfigs)
	{
		RdoSweepResult result = RunRdoSweepOne(pixels, iWidth, iHeight, rConfig.uiLookback, rConfig.fLambda, rConfig.iUberLevel);
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} uber={} encode={:7.2f}s deflated={:10} saved={:5.1f}%",
			result.uiLookback, result.fLambda, result.iUberLevel, result.fEncodeSeconds, result.iDeflatedBytes, result.fDeflatePercentSaved);
	}

	return 0;
}

template <IsExportJob T>
bool RunExportJobs()
{
	bool bDirty = gpFileManager->mbCleanExport;

	std::filesystem::path manifestFile = gpFileManager->mOutputDirectory;
	manifestFile /= T::kName;
	manifestFile += ".manifest";
	bDirty |= !std::filesystem::exists(manifestFile);

	std::filesystem::path packFile = gpFileManager->mOutputDirectory;
	packFile /= T::kName;
	packFile += ".pack";
	bDirty |= !std::filesystem::exists(packFile);

	std::filesystem::path headerFile = gpFileManager->mOutputDirectory;
	headerFile /= T::kName;
	headerFile += ".h";
	bDirty |= !std::filesystem::exists(headerFile);

	std::vector<std::unique_ptr<T>> exportJobs;
	for (const std::filesystem::path& rBaseDirectory : gpFileManager->mpInputDirectories)
	{
		for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::recursive_directory_iterator(rBaseDirectory))
		{
			std::optional<common::ChunkFlags_t> optionalChunkFlags = T::Handles(rDirectoryEntry);
			if (optionalChunkFlags.has_value())
			{
				std::unique_ptr<T>& rpExportJob = exportJobs.emplace_back(std::make_unique<T>(optionalChunkFlags.value(), rDirectoryEntry.path()));
				bDirty |= rpExportJob->CheckDirty(packFile);
			}
		}
	}

	if (!bDirty)
	{
		return true;
	}

	LOG(kDefault, kDebug, "\"{}\" is dirty, running export", T::kName);
	ScopedLogIndent scopedLogIndent;

	// Sort by relative path to ensure chunks are in same order inside the file (for more efficient Steam patching)
	std::sort(exportJobs.begin(), exportJobs.end(), [] (const std::unique_ptr<T>& rpA, const std::unique_ptr<T>& rpB) { return common::ToLower(rpA->mRelativeDirectory.string()) < common::ToLower(rpB->mRelativeDirectory.string()); });

	// Run the jobs
	for (std::unique_ptr<T>& rpExportJob : exportJobs)
	{
		rpExportJob->mFuture = std::async(std::launch::async, &T::RunExport, rpExportJob.get());
	}

	// Open temporary manifest file and write header
	std::filesystem::path temporaryManifestFile = gpFileManager->mTempDirectory;
	temporaryManifestFile /= T::kName;
	temporaryManifestFile += ".manifest";
	std::fstream temporaryManifestFileStream(temporaryManifestFile, std::ios::out | std::ios::binary);

	common::DataHeader dataHeader {};
	dataHeader.iMagic = common::DataHeader::kiMagic;
	dataHeader.iVersion = common::DataHeader::kiVersion;
	dataHeader.iChunkCount = exportJobs.size();
	temporaryManifestFileStream.write(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
	common::AlignOutputStream(temporaryManifestFileStream);

	// Open temporary pack file
	std::filesystem::path temporaryPackFile = gpFileManager->mTempDirectory;
	temporaryPackFile /= T::kName;
	temporaryPackFile += ".pack";
	std::fstream temporaryPackFileStream(temporaryPackFile, std::ios::out | std::ios::binary);

	// Open temporary header file and write header
	std::filesystem::path temporaryHeaderFile = gpFileManager->mTempDirectory;
	temporaryHeaderFile /= T::kName;
	temporaryHeaderFile += ".h";
	std::fstream temporaryHeaderFileStream(temporaryHeaderFile, std::ios::out);
	temporaryHeaderFileStream << "#pragma once" << std::endl;
	temporaryHeaderFileStream << std::endl;
	temporaryHeaderFileStream << "// This file is automatically generated by the Data Packer" << std::endl;
	temporaryHeaderFileStream << "// To add a Crc to this file place your data file in: BrokenEngine/Engine/Data" << std::endl;
	temporaryHeaderFileStream << "// or BrokenEngine/Projects/YOUR_PROJECT/Data" << std::endl;
	temporaryHeaderFileStream << "// Then re-build and the Data Packer will run automatically as a pre-build event" << std::endl;
	temporaryHeaderFileStream << std::endl;
	temporaryHeaderFileStream << "namespace data" << std::endl;
	temporaryHeaderFileStream << "{" << std::endl;
	temporaryHeaderFileStream << std::endl;

	bool bFailed = false;
	for (std::unique_ptr<T>& rpExportJob : exportJobs)
	{
		try
		{
			std::vector<std::byte>& rData = rpExportJob->mFuture.get();

			common::ChunkLocation chunkLocation =
			{
				.crc = rpExportJob->mCrc,
				.uiOffset = static_cast<uint64_t>(temporaryPackFileStream.tellp()),
				.uiSize = rData.size(),
			};
			temporaryManifestFileStream.write(reinterpret_cast<char*>(&chunkLocation), sizeof(chunkLocation));

			temporaryPackFileStream.write(reinterpret_cast<char*>(rData.data()), rData.size());
			common::AlignOutputStream(temporaryPackFileStream);

			temporaryHeaderFileStream << "inline constexpr common::crc_t k" << common::PathToCppVariable(rpExportJob->mRelativeFile) << "Crc = " << rpExportJob->mCrc << ";" << std::endl;
		}
		catch (const std::exception& rException)
		{
			LOG(kDefault, kError, "Exception thrown from future: \"{}\"", rException.what());
			std::string message = std::format("Asset: {}\n\n{}", rpExportJob->mInputPath.string(), rException.what());
			Quit(message.c_str(), "Data Packer - Export Failed");
			bFailed = true;
		}
	}

	temporaryHeaderFileStream << std::endl;
	temporaryHeaderFileStream << "} // namespace data" << std::endl;

	temporaryManifestFileStream.close();
	temporaryPackFileStream.close();
	temporaryHeaderFileStream.close();

	if (bFailed)
	{
		LOG(kDefault, kError, "\n\n\nFAILED\n\n\n");

		std::filesystem::remove(temporaryManifestFile);
		std::filesystem::remove(temporaryPackFile);
		std::filesystem::remove(temporaryHeaderFile);
	}
	else
	{

		std::filesystem::rename(temporaryManifestFile, manifestFile);
		std::filesystem::rename(temporaryPackFile, packFile);

		// Only copy header if it has changed (causes game re-compilation otherwise)
		if (!common::ContentsEqual(temporaryHeaderFile, headerFile))
		{
			std::filesystem::rename(temporaryHeaderFile, headerFile);
		}
	}

	return !bFailed;
}

bool MainThread(int argc, char* argv[])
{
	common::ThreadLocal threadLocal(1024, std::nullopt, false);

	LOG(kDefault, kDebug, "\nData Packer");
	LogIndent(1);

	VERIFY_SUCCESS(XMVerifyCPUSupport());

	Texture::StaticInit();

	auto pFileManager = std::make_unique<FileManager>(std::span(argv, argc));

	MigrateLegacyIntermediates();

	bool bSuccess = true;

	// Scene and Island need to be first as they can create new textures and models
	bSuccess &= RunExportJobs<ExportScene>();
	BakeIslandIntermediates();
	bSuccess &= RunExportJobs<ExportIsland>();

	GenerateIrradianceCubemaps();
	GeneratePreFilteredCubemaps();

	bSuccess &= RunExportJobs<ExportAudio>();
	bSuccess &= RunExportJobs<ExportFont>();
	bSuccess &= RunExportJobs<ExportModel>();
	bSuccess &= RunExportJobs<ExportShader>();
	bSuccess &= RunExportJobs<ExportTexture>();
	bSuccess &= RunExportJobs<ExportRaw>();

	// Generate DataTypes.h file with enum and array of data types (no CRC includes)
	std::stringstream dataTypesContent;
	dataTypesContent << "#pragma once" << std::endl;
	dataTypesContent << std::endl;
	dataTypesContent << "// This file is automatically generated by the Data Packer" << std::endl;
	dataTypesContent << std::endl;
	dataTypesContent << "namespace data" << std::endl;
	dataTypesContent << "{" << std::endl;
	dataTypesContent << std::endl;
	dataTypesContent << "enum DataTypes" << std::endl;
	dataTypesContent << "{" << std::endl;
	dataTypesContent << "\tkDataTypeAudio," << std::endl;
	dataTypesContent << "\tkDataTypeFont," << std::endl;
	dataTypesContent << "\tkDataTypeScene," << std::endl;
	dataTypesContent << "\tkDataTypeIslands," << std::endl;
	dataTypesContent << "\tkDataTypeModel," << std::endl;
	dataTypesContent << "\tkDataTypeShader," << std::endl;
	dataTypesContent << "\tkDataTypeTexture," << std::endl;
	dataTypesContent << "\tkDataTypeRaw," << std::endl;
	dataTypesContent << "" << std::endl;
	dataTypesContent << "\tkDataTypeCount" << std::endl;
	dataTypesContent << "};" << std::endl;
	dataTypesContent << std::endl;
	dataTypesContent << "inline constexpr const char* kpcDataTypeNames[kDataTypeCount] =" << std::endl;
	dataTypesContent << "{" << std::endl;
	dataTypesContent << "\t\"Audio\"," << std::endl;
	dataTypesContent << "\t\"Font\"," << std::endl;
	dataTypesContent << "\t\"Scene\"," << std::endl;
	dataTypesContent << "\t\"Islands\"," << std::endl;
	dataTypesContent << "\t\"Model\"," << std::endl;
	dataTypesContent << "\t\"Shader\"," << std::endl;
	dataTypesContent << "\t\"Texture\"," << std::endl;
	dataTypesContent << "\t\"Raw\"" << std::endl;
	dataTypesContent << "};" << std::endl;
	dataTypesContent << std::endl;
	dataTypesContent << "} // namespace data" << std::endl;

	std::string dataTypesString = dataTypesContent.str();

	std::filesystem::path dataTypesPath = gpFileManager->mOutputDirectory;
	dataTypesPath /= "DataTypes.h";

	WriteIfChanged(dataTypesString, dataTypesPath, "DataTypes.h");

	// Generate Data.h file with DataTypes.h include and all CRC headers
	std::stringstream dataHeaderContent;
	dataHeaderContent << "#pragma once" << std::endl;
	dataHeaderContent << std::endl;
	dataHeaderContent << "// This file is automatically generated by the Data Packer" << std::endl;
	dataHeaderContent << std::endl;
	dataHeaderContent << "#include \"DataTypes.h\"" << std::endl;
	dataHeaderContent << std::endl;
	dataHeaderContent << "#include \"Audio.h\"" << std::endl;
	dataHeaderContent << "#include \"Font.h\"" << std::endl;
	dataHeaderContent << "#include \"Scene.h\"" << std::endl;
	dataHeaderContent << "#include \"Islands.h\"" << std::endl;
	dataHeaderContent << "#include \"Model.h\"" << std::endl;
	dataHeaderContent << "#include \"Shader.h\"" << std::endl;
	dataHeaderContent << "#include \"Texture.h\"" << std::endl;
	dataHeaderContent << "#include \"Raw.h\"" << std::endl;

	std::string dataHeaderString = dataHeaderContent.str();

	std::filesystem::path dataHeaderPath = gpFileManager->mOutputDirectory;
	dataHeaderPath /= "Data.h";

	WriteIfChanged(dataHeaderString, dataHeaderPath, "Data.h");

	// Copy license files from ThirdParty directories to Attribution directory in output
	attribution::CopyThirdPartyLicenses(gpFileManager->mOutputDirectory);

	LOG(kDefault, kDebug, "");

	return bSuccess;
}

void Quit(const char* message, const char* title)
{
	fflush(stdout);
	MessageBox(nullptr, message, title, MB_OK | MB_SYSTEMMODAL);
}

int main(int argc, char* argv[])
{
	// Prevent multiple instances from running simultaneously
	HANDLE hMutex = CreateMutex(nullptr, TRUE, "BrokenEngineDataPacker");
	__assume(hMutex != nullptr);
	auto mutexDeleter = [](void* pH) { ReleaseMutex(pH); CloseHandle(pH); };
	std::unique_ptr<void, decltype(mutexDeleter)> pMutex(hMutex, mutexDeleter);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		printf("DataPacker is already running, waiting...\n");
		WaitForSingleObject(hMutex, INFINITE);
	}

	auto runOnce = [&]() -> bool
	{
		if (argc >= 2 && std::string_view(argv[1]) == "--rdo-sweep")
		{
			ASSERT(argc == 3);
			common::ThreadLocal threadLocal(1024, std::nullopt, false);
			Texture::StaticInit();
			return RunRdoSweep(argv[2]) == 0;
		}
		if (argc >= 2 && std::string_view(argv[1]) == "--rdo-sweep-full")
		{
			ASSERT(argc == 3);
			common::ThreadLocal threadLocal(1024, std::nullopt, false);
			Texture::StaticInit();
			return RunRdoSweepFull(argv[2]) == 0;
		}
		if (argc >= 2 && std::string_view(argv[1]) == "--rdo-sweep-validate")
		{
			ASSERT(argc == 3);
			common::ThreadLocal threadLocal(1024, std::nullopt, false);
			Texture::StaticInit();
			return RunRdoSweepValidate(argv[2]) == 0;
		}
		return MainThread(argc, argv);
	};

	bool bSuccess = false;

	if (IsDebuggerPresent() == TRUE)
	{
		bSuccess = runOnce();
	}
	else
	{
		try
		{
			bSuccess = runOnce();
		}
		catch (const std::exception& rException)
		{
			Quit(rException.what(), "Data Packer - std::exception");
		}
		catch (...)
		{
			Quit("", "Data Packer - Unknown exception");
		}
	}

	fflush(stdout);
	return bSuccess ? 0 : 1;
}

#if defined(_CRTDBG_MAP_ALLOC)

[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new(std::size_t n) noexcept(false) { void* p = malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n) noexcept(false) { void* p = malloc(n); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return _aligned_malloc(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return _aligned_malloc(n, static_cast<size_t>(al)); }

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete  (void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete  (void* p, std::size_t) noexcept { free(p); }
void operator delete[](void* p, std::size_t) noexcept { free(p); }
void operator delete  (void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete  (void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }

struct CrtBreakAllocSetter
{
	CrtBreakAllocSetter()
	{
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

		// Set to the allocation number from the CRT leak report to break on that allocation
		// _crtBreakAlloc = 5374;
	}
};

CrtBreakAllocSetter gCrtBreakAllocSetter;

#endif
