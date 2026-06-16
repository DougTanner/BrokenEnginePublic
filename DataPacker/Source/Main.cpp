#include "FileManager.h"

#include "Attribution.h"
#include "ExportJobs/ExportAudio.h"
#include "ExportJobs/ExportCubemapIbl.h"
#include "ExportJobs/ExportFont.h"
#include "ExportJobs/ExportScene.h"
#include "ExportJobs/ExportIsland.h"
#include "ExportJobs/ExportJob.h"
#include "ExportJobs/ExportModel.h"
#include "ExportJobs/ExportRaw.h"
#include "ExportJobs/ExportShader.h"
#include "ExportJobs/ExportTexture.h"
#include "ExportJobs/Island/BakeIslandIntermediates.h"
#include "ExportJobs/Texture/MigrateLegacyIntermediates.h"
#include "ExportJobs/Texture/Texture.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "bc7enc_rdo/bc7decomp.h"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

void Quit(const char* message, const char* title);

struct DataTypeEntry
{
	std::string_view enumSuffix;
	std::string_view displayName;
	std::string_view headerFile;
};

// Source of truth for the data-type enum, display names, and per-type CRC header
// includes. The order here defines the integer values of the generated DataTypes
// enum and is consumed by the runtime via `kpcDataTypeNames[kDataType...]`.
static constexpr DataTypeEntry kDataTypes[] =
{
	{"Audio",   "Audio",   "Audio.h"},
	{"Font",    "Font",    "Font.h"},
	{"Scene",   "Scene",   "Scene.h"},
	{"Islands", "Islands", "Islands.h"},
	{"Model",   "Model",   "Model.h"},
	{"Shader",  "Shader",  "Shader.h"},
	{"Texture", "Texture", "Texture.h"},
	{"Raw",     "Raw",     "Raw.h"},
};
static constexpr size_t kDataTypeCount = std::size(kDataTypes);

static void WriteIfChanged(const std::string& rContent, const std::filesystem::path& rPath, const char* logName)
{
	if (!common::ContentsEqual(rContent, rPath))
	{
		std::fstream stream(rPath, std::ios::out | std::ios::binary);
		stream << rContent;
		stream.close();
		VERIFY_SUCCESS(stream.good());
		LOG(kDefault, kDebug, "Re-generated {}", logName);
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
	Texture::EncodeWithRdo(bc7Output.data(), rPixels, iWidth, iHeight, VK_FORMAT_BC7_UNORM_BLOCK, fLambda, uiLookback, iUberLevel, {});
	auto tEnd = std::chrono::steady_clock::now();
	double fEncodeSeconds = std::chrono::duration<double>(tEnd - tStart).count();

	uLongf uiCompressedBound = compressBound(static_cast<uLong>(iBc7Size));
	std::vector<std::byte> deflated(uiCompressedBound);
	uLongf uiDeflatedSize = uiCompressedBound;
	int iZlibResult = compress2(reinterpret_cast<Bytef*>(deflated.data()), &uiDeflatedSize, reinterpret_cast<const Bytef*>(bc7Output.data()), static_cast<uLong>(iBc7Size), Z_BEST_COMPRESSION);
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

static int RunRdoSweepFull(const std::filesystem::path& rPath)
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
	ASSERT(fileStream.good());
	ASSERT(riWidth > 0 && riHeight > 0 && iMipMaps > 0);

	int64_t iCompressedSize = iFileSize - iHeaderSize;
	ASSERT(iCompressedSize > 0);
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
		{4096u, 0.5f, 2},   // former production knobs, for direct comparison (current knobs: constants atop ExportJobs/Texture/Texture.cpp)
	}};

	for (const ConfigToTest& rConfig : kConfigs)
	{
		RdoSweepResult result = RunRdoSweepOne(pixels, iWidth, iHeight, rConfig.uiLookback, rConfig.fLambda, rConfig.iUberLevel);
		LOG(kDefault, kInfo, "  lookback={:5} lambda={:4.2f} uber={} encode={:7.2f}s deflated={:10} saved={:5.1f}%", result.uiLookback, result.fLambda, result.iUberLevel, result.fEncodeSeconds, result.iDeflatedBytes, result.fDeflatePercentSaved);
	}

	return 0;
}

template <IsExportJob T>
static void WriteCrcHeader(const std::filesystem::path& rOutPath, const std::vector<std::unique_ptr<T>>& rJobs)
{
	std::fstream stream(rOutPath, std::ios::out | std::ios::binary);
	stream << "#pragma once" << std::endl;
	stream << std::endl;
	stream << "// This file is automatically generated by the Data Packer" << std::endl;
	stream << "// To add a Crc to this file place your data file in: BrokenEngine/Engine/Data" << std::endl;
	stream << "// or BrokenEngine/Projects/YOUR_PROJECT/Data" << std::endl;
	stream << "// Then re-build and the Data Packer will run automatically as a pre-build event" << std::endl;
	stream << std::endl;
	stream << "namespace data" << std::endl;
	stream << "{" << std::endl;
	stream << std::endl;
	for (const std::unique_ptr<T>& rpJob : rJobs)
	{
		stream << "inline constexpr common::crc_t k" << common::PathToCppVariable(rpJob->mRelativeFile) << "Crc = " << rpJob->mCrc << "ull;" << std::endl;
	}
	stream << std::endl;
	stream << "} // namespace data" << std::endl;
	stream.close();
	VERIFY_SUCCESS(stream.good());
}

template <IsExportJob T>
bool RunExportJobs()
{
	bool bDirty = gpFileManager->mbCleanExport;

	std::filesystem::path manifestFile = gpFileManager->mOutputDirectory;
	manifestFile /= T::kName;
	manifestFile += ".manifest";
	bDirty |= !std::filesystem::exists(manifestFile);

	int64_t iManifestChunkCount = -1;
	if (!bDirty)
	{
		// A lone kiVersion bump must re-export everything — the engine ASSERTs on a stale-version manifest and there is no recovery CLI
		common::DataHeader dataHeader {};
		std::fstream manifestFileStream(manifestFile, std::ios::in | std::ios::binary);
		manifestFileStream.read(reinterpret_cast<char*>(&dataHeader), sizeof(dataHeader));
		bDirty |= !manifestFileStream || dataHeader.iMagic != common::DataHeader::kiMagic || dataHeader.iVersion != common::DataHeader::kiVersion;
		iManifestChunkCount = dataHeader.iChunkCount;
	}

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

	// A deleted source asset shrinks the job list but dirties nothing above, so the stale chunk would
	// persist in .pack/.manifest (and its constant in the generated header) until an unrelated edit.
	// Comparing the manifest's chunk count against the live job count is the cheap deleted-asset
	// detector (iManifestChunkCount stays -1 when the manifest was missing/invalid, but bDirty is
	// already set in that case so the comparison is moot).
	bDirty |= iManifestChunkCount != static_cast<int64_t>(exportJobs.size());

	if (!bDirty)
	{
		return true;
	}

	LOG(kDefault, kDebug, "\"{}\" is dirty, running export", T::kName);
	ScopedLogIndent scopedLogIndent;

	// Sort by relative path to ensure chunks are in same order inside the file (for more efficient Steam patching)
	std::sort(exportJobs.begin(), exportJobs.end(), [] (const std::unique_ptr<T>& rpA, const std::unique_ptr<T>& rpB) { return common::ToLower(rpA->mRelativeFile) < common::ToLower(rpB->mRelativeFile); });

	// The two input roots (engine Data + project Data) can hold the same relative path; ExportJob
	// derives mRelativeFile/mCrc (mCrc = Crc(mRelativeFile)) from whichever root matched, so the
	// duplicates would silently produce two manifest entries with one CRC (ambiguous runtime lookup)
	// plus two identically named generated constants. Case-folded duplicates sort adjacent under the
	// comparator above, so the adjacent-pair walk catches every such collision; guaranteeing unique
	// lowered keys also makes the sort fully deterministic (no tied keys for std::sort to order
	// arbitrarily). A CRC equality check is intentionally omitted: identical paths already trip the
	// case-folded compare, and a hash collision between two DISTINCT paths is astronomically unlikely
	// and would not sort adjacent anyway — not the bug class this guards.
	for (size_t uiJob = 1; uiJob < exportJobs.size(); ++uiJob)
	{
		const T& rPrevious = *exportJobs.at(uiJob - 1);
		const T& rCurrent = *exportJobs.at(uiJob);
		if (common::ToLower(rPrevious.mRelativeFile) == common::ToLower(rCurrent.mRelativeFile))
		{
			throw std::runtime_error(std::format("\"{}\" export has a duplicate asset: \"{}\" and \"{}\" resolve to the same relative path across input roots \"{}\" and \"{}\". Rename or remove one so each chunk keeps a unique manifest key.", T::kName, rPrevious.mInputPath.string(), rCurrent.mInputPath.string(), gpFileManager->mpInputDirectories[0].string(), gpFileManager->mpInputDirectories[1].string()));
		}
	}

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

	std::filesystem::path temporaryHeaderFile = gpFileManager->mTempDirectory;
	temporaryHeaderFile /= T::kName;
	temporaryHeaderFile += ".h";

	std::vector<std::string> failureMessages;
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
		}
		catch (const std::exception& rException)
		{
			LOG(kDefault, kError, "Exception thrown from future: \"{}\"", rException.what());
			failureMessages.emplace_back(std::format("Asset: {}\n\n{}", rpExportJob->mInputPath.string(), rException.what()));
		}
	}

	temporaryManifestFileStream.close();
	temporaryPackFileStream.close();

	// Trust boundary: a disk-full / IO failure during the writes above sets badbit but leaves failureMessages
	// empty, so without this check the success path would rename truncated manifest/pack output over the good files.
	if (!temporaryManifestFileStream.good() || !temporaryPackFileStream.good())
	{
		failureMessages.emplace_back(std::format("Stream write failed for \"{}\" manifest/pack output", T::kName));
	}

	bool bFailed = !failureMessages.empty();
	if (bFailed)
	{
		LOG(kDefault, kError, "\n\n\nFAILED\n\n\n");

		// Defer to a single MessageBox so multi-failure runs don't stack modal dialogs.
		std::string combined;
		for (size_t i = 0; i < failureMessages.size(); ++i)
		{
			if (i > 0) combined.append("\n\n");
			combined.append(failureMessages.at(i));
		}
		Quit(combined.c_str(), "Data Packer - Export Failed");

		std::filesystem::remove(temporaryManifestFile);
		std::filesystem::remove(temporaryPackFile);
	}
	else
	{
		WriteCrcHeader(temporaryHeaderFile, exportJobs);

		std::filesystem::rename(temporaryManifestFile, manifestFile);
		std::filesystem::rename(temporaryPackFile, packFile);

		// Only copy header if it has changed (causes game re-compilation otherwise)
		if (!common::ContentsEqual(temporaryHeaderFile, headerFile))
		{
			std::filesystem::rename(temporaryHeaderFile, headerFile);
		}
		else
		{
			std::filesystem::remove(temporaryHeaderFile);
		}
	}

	return !bFailed;
}

template <typename... Ts>
static bool RunAllMainExports()
{
	// Comma fold (not `&& ...`) so every export runs even if an earlier one fails;
	// matches the original explicit `bSuccess &= RunExportJobs<...>()` sequence.
	bool bSuccess = true;
	((bSuccess &= RunExportJobs<Ts>()), ...);
	return bSuccess;
}

static void GenerateDataTypesHeader(const std::filesystem::path& rOutPath)
{
	std::stringstream content;
	content << "#pragma once" << std::endl;
	content << std::endl;
	content << "// This file is automatically generated by the Data Packer" << std::endl;
	content << std::endl;
	content << "namespace data" << std::endl;
	content << "{" << std::endl;
	content << std::endl;
	content << "enum DataTypes" << std::endl;
	content << "{" << std::endl;
	for (const DataTypeEntry& rEntry : kDataTypes)
	{
		content << "\tkDataType" << rEntry.enumSuffix << "," << std::endl;
	}
	content << "" << std::endl;
	content << "\tkDataTypeCount" << std::endl;
	content << "};" << std::endl;
	content << std::endl;
	content << "inline constexpr const char* kpcDataTypeNames[kDataTypeCount] =" << std::endl;
	content << "{" << std::endl;
	for (size_t i = 0; i < kDataTypeCount; ++i)
	{
		content << "\t\"" << kDataTypes[i].displayName << "\"";
		if (i + 1 < kDataTypeCount)
		{
			content << ",";
		}
		content << std::endl;
	}
	content << "};" << std::endl;
	content << std::endl;
	content << "} // namespace data" << std::endl;

	WriteIfChanged(content.str(), rOutPath, "DataTypes.h");
}

static void GenerateDataHeader(const std::filesystem::path& rOutPath)
{
	std::stringstream content;
	content << "#pragma once" << std::endl;
	content << std::endl;
	content << "// This file is automatically generated by the Data Packer" << std::endl;
	content << std::endl;
	content << "#include \"DataTypes.h\"" << std::endl;
	content << std::endl;
	for (const DataTypeEntry& rEntry : kDataTypes)
	{
		content << "#include \"" << rEntry.headerFile << "\"" << std::endl;
	}

	WriteIfChanged(content.str(), rOutPath, "Data.h");
}

bool MainThread(int argc, char* argv[])
{
	common::ThreadLocal threadLocal(1024, std::nullopt, false);

	LOG(kDefault, kDebug, "\nData Packer");
	ScopedLogIndent scopedLogIndent;

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

	bSuccess &= RunAllMainExports<ExportAudio, ExportFont, ExportModel, ExportShader, ExportTexture, ExportRaw>();

	GenerateDataTypesHeader(gpFileManager->mOutputDirectory / "DataTypes.h");
	GenerateDataHeader(gpFileManager->mOutputDirectory / "Data.h");

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
		std::printf("DataPacker is already running, waiting...\n");
		WaitForSingleObject(hMutex, INFINITE);
	}

	auto runOnce = [&]() -> bool
	{
		if (argc >= 2 && std::string_view(argv[1]).starts_with("--rdo-sweep"))
		{
			// CLI trust boundary: each sweep mode takes exactly one image/intermediate path
			if (argc != 3)
			{
				std::printf("%s requires exactly one <image path> argument\n", argv[1]);
				return false;
			}
			common::ThreadLocal threadLocal(1024, std::nullopt, false);
			Texture::StaticInit();
			if (std::string_view(argv[1]) == "--rdo-sweep")
			{
				return RunRdoSweep(argv[2]) == 0;
			}
			if (std::string_view(argv[1]) == "--rdo-sweep-full")
			{
				return RunRdoSweepFull(argv[2]) == 0;
			}
			if (std::string_view(argv[1]) == "--rdo-sweep-validate")
			{
				return RunRdoSweepValidate(argv[2]) == 0;
			}
			std::printf("Unknown mode %s\n", argv[1]);
			return false;
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

[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new(std::size_t n) noexcept(false) { void* p = std::malloc(n); __assume(p != nullptr); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n) noexcept(false) { void* p = std::malloc(n); __assume(p != nullptr); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return std::malloc(n); }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al) noexcept(false) { void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p != nullptr); return p; }
[[nodiscard]] _Ret_notnull_ _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al) noexcept(false) { void* p = _aligned_malloc(n, static_cast<size_t>(al)); __assume(p != nullptr); return p; }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new  (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return _aligned_malloc(n, static_cast<size_t>(al)); }
[[nodiscard]] _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n) void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept { return _aligned_malloc(n, static_cast<size_t>(al)); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete  (void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete  (void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
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
