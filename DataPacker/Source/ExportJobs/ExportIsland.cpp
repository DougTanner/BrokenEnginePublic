#include "ExportIsland.h"

#include "BakeIslandIntermediates.h"
#include "FileManager.h"
#include "Texture.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "bc7enc_rdo/bc7decomp.h"
#include "tinygltf/json.hpp"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

using enum common::ChunkFlags;

namespace
{

// Total vertical span assumed for legacy prebaked islands (no Island.json / .terrain source).
// Their R16_UNORM normalized heightmaps get scaled by this then offset by kfOceanDepthMeters to
// produce engine-meters. Picked to match the new-pipeline archetype's default Terrain.Height —
// if a legacy island's original Gaea bake used a different elevation span, physical heights
// will be off by that factor.
constexpr float kfLegacyIslandElevationMeters = 1000.0f;

} // namespace

// ---------------------------------------------------------------------------
// Legacy helpers (pre-mip islands: Islands/01/ and similar pre-baked content)
// ---------------------------------------------------------------------------

// Decompress a Texture::Save'd intermediate file. Current format: 8-byte magic + 3x int64 header
// + zlib stream of all mips concatenated. Legacy format: same minus the magic (the migration in
// Main.cpp updates pre-magic files in place, but this reader stays tolerant for safety).
static std::vector<std::byte> LoadIntermediate(const std::filesystem::path& rPath, int64_t& riWidth, int64_t& riHeight, int64_t& riMipMaps, VkFormat vkFormat)
{
	std::fstream fileStream(rPath, std::ios::in | std::ios::binary);
	int64_t iFileSize = std::filesystem::file_size(rPath);
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
	fileStream.read(reinterpret_cast<char*>(&riMipMaps), sizeof(riMipMaps));
	int64_t iCompressedSize = iFileSize - iHeaderSize;
	std::vector<std::byte> compressed(iCompressedSize);
	fileStream.read(reinterpret_cast<char*>(compressed.data()), iCompressedSize);
	fileStream.close();

	int64_t iUncompressedSize = 0;
	int64_t iMipWidth = riWidth;
	int64_t iMipHeight = riHeight;
	for (int64_t i = 0; i < riMipMaps; ++i)
	{
		iUncompressedSize += common::SizeInBytes(vkFormat, iMipWidth, iMipHeight);
		iMipWidth /= 2;
		iMipHeight /= 2;
	}

	std::vector<std::byte> data(iUncompressedSize);
	uLongf uiUncompressedSize = static_cast<uLongf>(iUncompressedSize);
	int iZlibResult = uncompress(reinterpret_cast<Bytef*>(data.data()), &uiUncompressedSize, reinterpret_cast<const Bytef*>(compressed.data()), static_cast<uLong>(iCompressedSize));
	ASSERT(iZlibResult == Z_OK);
	return data;
}

// ---------------------------------------------------------------------------
// New helpers (per-mip islands: Islands/02/ and beyond)
// ---------------------------------------------------------------------------

// Mip size derived from headerless uint16 square texture byte count. AmbientOcclusion.r16 is the
// sentinel rather than Elevation.r32 because elevation is omitted at mips below iElevationStart
// (see kiElevationDivisor in ExportIsland.h).
static int64_t DeriveMipSize(const std::filesystem::path& rRawFile)
{
	int64_t iFileSize = std::filesystem::file_size(rRawFile);
	int64_t iSampleCount = iFileSize / static_cast<int64_t>(sizeof(uint16_t));
	int64_t iSize = static_cast<int64_t>(std::sqrt(static_cast<double>(iSampleCount)));
	ASSERT(iSize * iSize == iSampleCount);
	return iSize;
}

static std::vector<std::filesystem::path> EnumerateMipDirs(const std::filesystem::path& rIslandFolder)
{
	std::vector<std::filesystem::path> directories;
	for (size_t i = 0; ; ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		if (!std::filesystem::exists(mipDir / "AmbientOcclusion.r16"))
		{
			break;
		}
		directories.push_back(mipDir);
	}
	return directories;
}

// Construct a multi-mip Texture by loading each mip's source into a temp and stealing its mData.
// Caller must hold Texture::sEncodeMutex around the build + Save chain (per Texture::sEncodeMutex
// memory contract — keeps only one set of source pixels resident at a time).
static Texture BuildMipChain(const std::vector<std::filesystem::path>& rMipDirs, const std::vector<int64_t>& rMipSizes, const char* pcFilename, FileType eFileType, bool bFromGamma)
{
	bool bRawType = (eFileType == FileType::kUint16Raw || eFileType == FileType::kFloat32);
	Texture master(rMipDirs.at(0) / pcFilename, eFileType, bFromGamma, bRawType ? rMipSizes.at(0) : 0, bRawType ? rMipSizes.at(0) : 0);

	for (size_t i = 1; i < rMipDirs.size(); ++i)
	{
		Texture temp(rMipDirs.at(i) / pcFilename, eFileType, bFromGamma, bRawType ? rMipSizes.at(i) : 0, bRawType ? rMipSizes.at(i) : 0);
		ASSERT(temp.miWidth == master.miWidth >> i);
		ASSERT(temp.miHeight == master.miHeight >> i);
		master.mData.push_back(std::move(temp.mData.front()));
	}

	return master;
}

// ---------------------------------------------------------------------------
// Ingest paths
// ---------------------------------------------------------------------------

static void ExportLegacy(const std::filesystem::path& rInputPath, std::vector<float>& rCpuHeightmapData, int32_t& riHeightmapSize)
{
	std::filesystem::path ambientOcclusionFloat32File(rInputPath);
	ambientOcclusionFloat32File /= "AmbientOcclusion.r32";
	if (std::filesystem::exists(ambientOcclusionFloat32File))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(ambientOcclusionFloat32File, FileType::kFloat32, true, kiIslandSize, kiIslandSize);
		texture.Downsize(1);

		std::filesystem::path path(rInputPath);
		path /= kpcIslandAmbientOcclusion;
		texture.Save(path, VK_FORMAT_BC4_UNORM_BLOCK, false);

		std::filesystem::remove(ambientOcclusionFloat32File);
	}

	std::filesystem::path colorExrFile(rInputPath);
	colorExrFile /= "Color.exr";
	if (std::filesystem::exists(colorExrFile))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(colorExrFile, FileType::kExr, true);
		std::filesystem::path path(rInputPath);
		path /= kpcIslandColor;
		texture.Save(path, VK_FORMAT_BC7_UNORM_BLOCK, true);

		std::filesystem::remove(colorExrFile);
	}

	// One-time migration: pre-baked R16_UNORM normalized [0,1] heightmap → R32_SFLOAT meters.
	// Legacy islands have no Island.json / .terrain source carrying real elevation; assume
	// kfLegacyIslandElevationMeters total vertical span. Per-mip temp .r32 files feed Texture's
	// kFloat32 ingest so the existing multi-mip Save pipeline emits the new chunk.
	std::filesystem::path elevationR16File(rInputPath);
	elevationR16File /= "Elevation.R16_UNORM";
	if (std::filesystem::exists(elevationR16File))
	{
		LOG(kDefault, kWarning, "Legacy island \"{}\": migrating R16_UNORM normalized [0,1] heightmap to R32_SFLOAT meters with hardcoded {}m elevation span. Physical heights will be wrong if the original Gaea bake used a different Terrain.Height.", rInputPath.filename().string(), kfLegacyIslandElevationMeters);

		int64_t iWidth = 0;
		int64_t iHeight = 0;
		int64_t iMipMaps = 0;
		std::vector<std::byte> r16Data = LoadIntermediate(elevationR16File, iWidth, iHeight, iMipMaps, VK_FORMAT_R16_UNORM);

		std::vector<std::filesystem::path> tempFiles;
		tempFiles.reserve(static_cast<size_t>(iMipMaps));
		common::ScopedLambda tempCleanup([&tempFiles]()
		{
			for (const std::filesystem::path& rTempFile : tempFiles)
			{
				if (std::filesystem::exists(rTempFile))
				{
					std::filesystem::remove(rTempFile);
				}
			}
		});

		const uint16_t* puiR16 = reinterpret_cast<const uint16_t*>(r16Data.data());
		int64_t iMipWidth = iWidth;
		int64_t iMipHeight = iHeight;
		for (int64_t iMip = 0; iMip < iMipMaps; ++iMip)
		{
			std::filesystem::path tempPath = gpFileManager->mTempDirectory / std::format("{}-legacy-elev-mip{}.r32", rInputPath.filename().string(), iMip);
			tempFiles.push_back(tempPath);

			std::vector<float> floats(iMipWidth * iMipHeight);
			for (int64_t i = 0; i < iMipWidth * iMipHeight; ++i)
			{
				float fNormalized = common::UnormToFloat<uint16_t>(puiR16[i]);
				floats.at(i) = fNormalized * kfLegacyIslandElevationMeters - common::kfOceanDepthMeters;
			}

			std::ofstream stream(tempPath, std::ios::binary);
			stream.write(reinterpret_cast<const char*>(floats.data()), floats.size() * sizeof(float));
			stream.close();

			// Mip 0 → CPU heightmap (full resolution). Matches legacy ingest behavior.
			if (iMip == 0)
			{
				rCpuHeightmapData = std::move(floats);
				riHeightmapSize = static_cast<int32_t>(iMipWidth);
			}

			puiR16 += iMipWidth * iMipHeight;
			iMipWidth /= 2;
			iMipHeight /= 2;
		}

		{
			std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
			Texture master(tempFiles.at(0), FileType::kFloat32, false, iWidth, iHeight);
			int64_t iMasterMipWidth = iWidth / 2;
			int64_t iMasterMipHeight = iHeight / 2;
			for (int64_t iMip = 1; iMip < iMipMaps; ++iMip)
			{
				Texture temp(tempFiles.at(iMip), FileType::kFloat32, false, iMasterMipWidth, iMasterMipHeight);
				master.mData.push_back(std::move(temp.mData.front()));
				iMasterMipWidth /= 2;
				iMasterMipHeight /= 2;
			}

			std::filesystem::path path(rInputPath);
			path /= kpcIslandElevation;
			master.Save(path, VK_FORMAT_R32_SFLOAT, false);
		}
	}

	std::filesystem::path normalsExrFile(rInputPath);
	normalsExrFile /= "Normals.exr";
	if (std::filesystem::exists(normalsExrFile))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(normalsExrFile, FileType::kExr, true);

		std::filesystem::path path(rInputPath);
		path /= kpcIslandNormals;
		texture.Save(path, VK_FORMAT_BC5_UNORM_BLOCK, false);

		std::filesystem::remove(normalsExrFile);
	}
	else
	{
		// One-time migration: pre-baked islands shipped before BC5 support carry "Normals.BC7_UNORM_BLOCK".
		// Decode mip 0, drop the BC7 file, and re-encode through the standard BC5 mip pipeline.
		std::filesystem::path bc5NormalsFile(rInputPath);
		bc5NormalsFile /= kpcIslandNormals;
		std::filesystem::path bc7NormalsFile(rInputPath);
		bc7NormalsFile /= "Normals.BC7_UNORM_BLOCK";
		if (!std::filesystem::exists(bc5NormalsFile) && std::filesystem::exists(bc7NormalsFile))
		{
			std::fstream fileIn(bc7NormalsFile, std::ios::in | std::ios::binary);
			int64_t iWidth = 0;
			int64_t iHeight = 0;
			int64_t iMipMaps = 0;
			fileIn.read(reinterpret_cast<char*>(&iWidth), sizeof(iWidth));
			fileIn.read(reinterpret_cast<char*>(&iHeight), sizeof(iHeight));
			fileIn.read(reinterpret_cast<char*>(&iMipMaps), sizeof(iMipMaps));
			int64_t iMip0Size = common::SizeInBytes(VK_FORMAT_BC7_UNORM_BLOCK, iWidth, iHeight);
			std::vector<uint8_t> bc7Data(iMip0Size);
			fileIn.read(reinterpret_cast<char*>(bc7Data.data()), iMip0Size);
			fileIn.close();

			std::vector<uint8_t> rgbaData(iWidth * iHeight * 4);
			int64_t iBlocksX = iWidth / 4;
			int64_t iBlocksY = iHeight / 4;
			for (int64_t iBy = 0; iBy < iBlocksY; ++iBy)
			{
				for (int64_t iBx = 0; iBx < iBlocksX; ++iBx)
				{
					bc7decomp::color_rgba pixels[16];
					bc7decomp::unpack_bc7(bc7Data.data() + 16 * (iBy * iBlocksX + iBx), pixels);
					for (int64_t iPy = 0; iPy < 4; ++iPy)
					{
						for (int64_t iPx = 0; iPx < 4; ++iPx)
						{
							int64_t iDstX = iBx * 4 + iPx;
							int64_t iDstY = iBy * 4 + iPy;
							std::memcpy(&rgbaData.at(4 * (iDstY * iWidth + iDstX)), pixels[iPy * 4 + iPx].m_comps, 4);
						}
					}
				}
			}

			std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
			Texture texture(reinterpret_cast<const std::byte*>(rgbaData.data()), iWidth, iHeight, 4);
			texture.MakeMipmaps(VK_FORMAT_BC5_UNORM_BLOCK);
			texture.Save(bc5NormalsFile, VK_FORMAT_BC5_UNORM_BLOCK, false);

			std::filesystem::remove(bc7NormalsFile);
			LOG(kDefault, kInfo, "Migrated island normals BC7 -> BC5: {}", bc5NormalsFile.string());
		}
	}
}

static void ExportNew(const std::filesystem::path& rInputPath, std::vector<float>& rCpuHeightmapData, int32_t& riHeightmapSize, float& rfWorldFootprintMeters, float& rfWorldElevationMeters)
{
	std::vector<std::filesystem::path> mipDirs = EnumerateMipDirs(rInputPath);
	ASSERT(!mipDirs.empty());

	std::vector<int64_t> mipSizes;
	mipSizes.reserve(mipDirs.size());
	for (const std::filesystem::path& rMipDir : mipDirs)
	{
		mipSizes.push_back(DeriveMipSize(rMipDir / "AmbientOcclusion.r16"));
	}

	// Elevation sub-chain: starts at the first mip whose dimension is <= mips[0] / kiElevationDivisor
	// (see comment on kiElevationDivisor in ExportIsland.h). BakeIslandIntermediates is responsible
	// for deleting Elevation.r32 at lower mips, so we mirror the same start-index math here.
	size_t iElevationStart = IslandElevationStartIndex(mipSizes);
	ASSERT(iElevationStart < mipDirs.size());

	std::vector<std::filesystem::path> elevationMipDirs(mipDirs.begin() + iElevationStart, mipDirs.end());
	std::vector<int64_t> elevationMipSizes(mipSizes.begin() + iElevationStart, mipSizes.end());

	// World dimensions: Island.json override if present, else read from archetype .terrain.
	WorldDimensions worldDimensions = GetIslandDimensions(rInputPath);
	rfWorldFootprintMeters = worldDimensions.fFootprintMeters;
	rfWorldElevationMeters = worldDimensions.fElevationMeters;

	// Encode each intermediate as a multi-mip texture in turn. sEncodeMutex serializes the BC
	// encoder across textures (it uses all hardware threads internally; mutex bounds memory).
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture = BuildMipChain(mipDirs, mipSizes, "AmbientOcclusion.r16", FileType::kUint16Raw, false);
		texture.Save(rInputPath / kpcIslandAmbientOcclusion, VK_FORMAT_BC4_UNORM_BLOCK, false);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture = BuildMipChain(mipDirs, mipSizes, "Color.exr", FileType::kExr, true);
		texture.Save(rInputPath / kpcIslandColor, VK_FORMAT_BC7_UNORM_BLOCK, true);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture = BuildMipChain(elevationMipDirs, elevationMipSizes, "Elevation.r32", FileType::kFloat32, false);
		texture.Save(rInputPath / kpcIslandElevation, VK_FORMAT_R32_SFLOAT, false);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture = BuildMipChain(mipDirs, mipSizes, "Normals.exr", FileType::kExr, true);
		texture.Save(rInputPath / kpcIslandNormals, VK_FORMAT_BC5_UNORM_BLOCK, false);
	}

	// CPU heightmap: float copy of one mip's elevation, packed into the chunk data payload for
	// runtime nav / world-Z queries. Source is the same Elevation.r32 BakeIslandIntermediates
	// pre-offset by kfOceanDepthMeters so values are engine-meters (beach = 0, ocean = negative).
	int64_t iCpuMipSize = mipSizes.at(iElevationStart);
	rCpuHeightmapData.resize(iCpuMipSize * iCpuMipSize);
	{
		std::fstream rawStream(mipDirs.at(iElevationStart) / "Elevation.r32", std::ios::in | std::ios::binary);
		rawStream.read(reinterpret_cast<char*>(rCpuHeightmapData.data()), rCpuHeightmapData.size() * sizeof(float));
		rawStream.close();
	}

	riHeightmapSize = static_cast<int32_t>(iCpuMipSize);
}

// ---------------------------------------------------------------------------
// ExportJob entry points
// ---------------------------------------------------------------------------

std::optional<common::ChunkFlags_t> ExportIsland::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (!rDirectoryEntry.is_directory() || rDirectoryEntry.path().parent_path().filename() != "Islands")
	{
		return std::nullopt;
	}

	const std::filesystem::path& rPath = rDirectoryEntry.path();
	// AmbientOcclusion.r16 is the per-mip sentinel: Elevation.r32 is dropped at mips above
	// `mips[0] / kiElevationDivisor` so it can't gate detection. Legacy prebaked islands carry
	// Elevation.R16_UNORM at the root (no mip dirs).
	bool bHasNewLayout = std::filesystem::exists(rPath / "mip0" / "AmbientOcclusion.r16");
	bool bHasLegacyLayout = std::filesystem::exists(rPath / "Elevation.R16_UNORM");
	return (bHasNewLayout || bHasLegacyLayout) ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kIsland) : std::nullopt;
}

void ExportIsland::Export()
{
	std::vector<float> cpuHeightmapData;
	int32_t iHeightmapSize = 0;
	float fWorldFootprintMeters = 0.0f;
	float fWorldElevationMeters = 0.0f;

	// Mirror Handles(): AmbientOcclusion.r16 is the per-mip sentinel since Elevation.r32 is
	// dropped at mip 0 for islands where mips[0] > mips[0] / kiElevationDivisor (the common case).
	if (std::filesystem::exists(mInputPath / "mip0" / "AmbientOcclusion.r16"))
	{
		ExportNew(mInputPath, cpuHeightmapData, iHeightmapSize, fWorldFootprintMeters, fWorldElevationMeters);
	}
	else
	{
		ExportLegacy(mInputPath, cpuHeightmapData, iHeightmapSize);
	}

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	int64_t iHeightmapDataSize = static_cast<int64_t>(cpuHeightmapData.size() * sizeof(float));
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iHeightmapDataSize);

	std::filesystem::path ambientOcclusionFile(relativeFile);
	ambientOcclusionFile /= kpcIslandAmbientOcclusion;
	pHeader->islandHeader.ambientOcclusionCrc = common::Crc(ambientOcclusionFile.string());

	std::filesystem::path colorsFile(relativeFile);
	colorsFile /= kpcIslandColor;
	pHeader->islandHeader.colorsCrc = common::Crc(colorsFile.string());

	std::filesystem::path elevationFile(relativeFile);
	elevationFile /= kpcIslandElevation;
	pHeader->islandHeader.elevationCrc = common::Crc(elevationFile.string());

	std::filesystem::path normalsFile(relativeFile);
	normalsFile /= kpcIslandNormals;
	pHeader->islandHeader.normalsCrc = common::Crc(normalsFile.string());

	pHeader->islandHeader.iHeightmapSize = iHeightmapSize;
	pHeader->islandHeader.fWorldFootprintMeters = fWorldFootprintMeters;
	pHeader->islandHeader.fWorldElevationMeters = fWorldElevationMeters;

	std::memcpy(dataSpan.data(), cpuHeightmapData.data(), iHeightmapDataSize);
}
