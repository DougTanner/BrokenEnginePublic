#include "ExportIsland.h"

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

// Mip 0 size derived from `Elevation.r16` byte count (headerless uint16 square texture).
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
	std::vector<std::filesystem::path> dirs;
	for (size_t i = 0; ; ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		if (!std::filesystem::exists(mipDir / "Elevation.r16"))
		{
			break;
		}
		dirs.push_back(mipDir);
	}
	return dirs;
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

static void ExportLegacy(const std::filesystem::path& rInputPath, std::vector<float>& rCpuHeightmapData, int32_t& riHeightmapWidth, int32_t& riHeightmapHeight, uint16_t& ruiBeachElevation)
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

	std::filesystem::path elevationFloat32File(rInputPath);
	elevationFloat32File /= "Elevation.r32";
	if (std::filesystem::exists(elevationFloat32File))
	{
		int64_t iElevationFileSize = std::filesystem::file_size(elevationFloat32File);
		int64_t iSourceSize = static_cast<int64_t>(std::sqrt(static_cast<double>(iElevationFileSize) / sizeof(float)));
		ASSERT(iSourceSize * iSourceSize * static_cast<int64_t>(sizeof(float)) == iElevationFileSize);

		{
			Texture cpuTexture(elevationFloat32File, FileType::kFloat32, false, iSourceSize, iSourceSize);
			cpuTexture.Downsize(2);

			riHeightmapWidth = static_cast<int32_t>(cpuTexture.miWidth);
			riHeightmapHeight = static_cast<int32_t>(cpuTexture.miHeight);
			rCpuHeightmapData = cpuTexture.mData.at(0);
		}

		{
			std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
			Texture gpuTexture(elevationFloat32File, FileType::kFloat32, false, iSourceSize, iSourceSize);
			gpuTexture.Downsize(2);

			std::filesystem::path path(rInputPath);
			path /= kpcIslandElevation;
			gpuTexture.Save(path, VK_FORMAT_R16_UNORM, false);
		}

		std::filesystem::remove(elevationFloat32File);
	}

	// Fallback: load already-processed R16_UNORM if r32 didn't exist (pre-baked islands).
	if (rCpuHeightmapData.empty())
	{
		std::filesystem::path elevationR16File(rInputPath);
		elevationR16File /= kpcIslandElevation;

		if (std::filesystem::exists(elevationR16File))
		{
			int64_t iWidth = 0;
			int64_t iHeight = 0;
			int64_t iMipMaps = 0;
			std::vector<std::byte> data = LoadIntermediate(elevationR16File, iWidth, iHeight, iMipMaps, VK_FORMAT_R16_UNORM);

			int64_t iPixelCount = iWidth * iHeight;
			const uint16_t* puiR16 = reinterpret_cast<const uint16_t*>(data.data());

			rCpuHeightmapData.resize(iPixelCount);
			for (int64_t i = 0; i < iPixelCount; ++i)
			{
				rCpuHeightmapData.at(i) = common::UnormToFloat<uint16_t>(puiR16[i]);
			}

			riHeightmapWidth = static_cast<int32_t>(iWidth);
			riHeightmapHeight = static_cast<int32_t>(iHeight);
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

	// Beach elevation
	std::filesystem::path elevationU16File(rInputPath);
	elevationU16File /= kpcIslandElevation;
	int64_t iElevationWidth = 0;
	int64_t iElevationHeight = 0;
	int64_t iElevationMipMaps = 0;
	std::vector<std::byte> dataU16 = LoadIntermediate(elevationU16File, iElevationWidth, iElevationHeight, iElevationMipMaps, VK_FORMAT_R16_UNORM);

	uint16_t* puiPixels = reinterpret_cast<uint16_t*>(dataU16.data());
	std::unordered_map<uint16_t, int64_t> map;
	int64_t iElevationSize = kiIslandSize / kiElevationDivisor;
	for (int64_t i = 0; i < iElevationSize * iElevationSize; ++i)
	{
		map[puiPixels[i]]++;
	}

	int64_t iMaxCount = 0;
	for (const auto& [ruiElevation, riCount] : map)
	{
		if (ruiElevation != 0 && riCount > iMaxCount)
		{
			iMaxCount = riCount;
			ruiBeachElevation = ruiElevation;
		}
	}
	LOG(kDefault, kDebug, "Beach elevation (legacy): {} {} ({} times)", ruiBeachElevation, common::UnormToFloat(ruiBeachElevation), iMaxCount);
}

static void ExportNew(const std::filesystem::path& rInputPath, std::vector<float>& rCpuHeightmapData, int32_t& riHeightmapWidth, int32_t& riHeightmapHeight, uint16_t& ruiBeachElevation, float& rfWorldWidthMeters, float& rfWorldHeightMeters)
{
	std::vector<std::filesystem::path> mipDirs = EnumerateMipDirs(rInputPath);
	ASSERT(!mipDirs.empty());

	std::vector<int64_t> mipSizes;
	mipSizes.reserve(mipDirs.size());
	for (const std::filesystem::path& rMipDir : mipDirs)
	{
		mipSizes.push_back(DeriveMipSize(rMipDir / "Elevation.r16"));
	}

	// World dimensions emitted by BakeIslandIntermediates after a successful Gaea bake.
	std::filesystem::path worldFile = rInputPath / "world.json";
	std::ifstream worldStream(worldFile);
	nlohmann::json worldJson = nlohmann::json::parse(worldStream);
	worldStream.close();
	rfWorldWidthMeters = worldJson.at("widthMeters").get<float>();
	rfWorldHeightMeters = worldJson.at("heightMeters").get<float>();

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
		Texture texture = BuildMipChain(mipDirs, mipSizes, "Elevation.r16", FileType::kUint16Raw, false);
		texture.Save(rInputPath / kpcIslandElevation, VK_FORMAT_R16_UNORM, false);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture = BuildMipChain(mipDirs, mipSizes, "Normals.exr", FileType::kExr, true);
		texture.Save(rInputPath / kpcIslandNormals, VK_FORMAT_BC5_UNORM_BLOCK, false);
	}

	// CPU heightmap: float copy of one mip's elevation, packed into the chunk data payload for
	// runtime nav / world-Z queries. Pick mip 2 to match the legacy 8192/4 = 2048 sampling
	// resolution; fall back to the smallest mip if the chain has fewer levels.
	size_t iCpuMipIndex = std::min<size_t>(2, mipDirs.size() - 1);
	int64_t iCpuMipSize = mipSizes.at(iCpuMipIndex);
	std::vector<uint16_t> cpuMipU16(iCpuMipSize * iCpuMipSize);
	{
		std::fstream rawStream(mipDirs.at(iCpuMipIndex) / "Elevation.r16", std::ios::in | std::ios::binary);
		rawStream.read(reinterpret_cast<char*>(cpuMipU16.data()), cpuMipU16.size() * sizeof(uint16_t));
		rawStream.close();
	}

	rCpuHeightmapData.resize(iCpuMipSize * iCpuMipSize);
	for (int64_t i = 0; i < iCpuMipSize * iCpuMipSize; ++i)
	{
		rCpuHeightmapData.at(i) = common::UnormToFloat<uint16_t>(cpuMipU16.at(i));
	}

	riHeightmapWidth = static_cast<int32_t>(iCpuMipSize);
	riHeightmapHeight = static_cast<int32_t>(iCpuMipSize);

	// Beach elevation: mode of nonzero uint16 values across mip 0's full elevation.
	int64_t iMip0Size = mipSizes.at(0);
	std::vector<uint16_t> mip0U16(iMip0Size * iMip0Size);
	{
		std::fstream rawStream(mipDirs.at(0) / "Elevation.r16", std::ios::in | std::ios::binary);
		rawStream.read(reinterpret_cast<char*>(mip0U16.data()), mip0U16.size() * sizeof(uint16_t));
		rawStream.close();
	}

	std::unordered_map<uint16_t, int64_t> histogram;
	for (uint16_t uiValue : mip0U16)
	{
		++histogram[uiValue];
	}

	int64_t iMaxCount = 0;
	for (const auto& [ruiElevation, riCount] : histogram)
	{
		if (ruiElevation != 0 && riCount > iMaxCount)
		{
			iMaxCount = riCount;
			ruiBeachElevation = ruiElevation;
		}
	}
	LOG(kDefault, kDebug, "Beach elevation (new): {} {} ({} times)", ruiBeachElevation, common::UnormToFloat(ruiBeachElevation), iMaxCount);
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
	bool bHasNewLayout = std::filesystem::exists(rPath / "mip0" / "Elevation.r16");
	bool bHasLegacyLayout = std::filesystem::exists(rPath / "Elevation.r32") || std::filesystem::exists(rPath / kpcIslandElevation);
	return (bHasNewLayout || bHasLegacyLayout) ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kIsland) : std::nullopt;
}

void ExportIsland::Export()
{
	std::vector<float> cpuHeightmapData;
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
	uint16_t uiBeachElevation = 0;
	float fWorldWidthMeters = 0.0f;
	float fWorldHeightMeters = 0.0f;

	if (std::filesystem::exists(mInputPath / "mip0" / "Elevation.r16"))
	{
		ExportNew(mInputPath, cpuHeightmapData, iHeightmapWidth, iHeightmapHeight, uiBeachElevation, fWorldWidthMeters, fWorldHeightMeters);
	}
	else
	{
		ExportLegacy(mInputPath, cpuHeightmapData, iHeightmapWidth, iHeightmapHeight, uiBeachElevation);
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

	pHeader->islandHeader.uiBeachElevation = uiBeachElevation;
	pHeader->islandHeader.iHeightmapWidth = iHeightmapWidth;
	pHeader->islandHeader.iHeightmapHeight = iHeightmapHeight;
	pHeader->islandHeader.fWorldWidthMeters = fWorldWidthMeters;
	pHeader->islandHeader.fWorldHeightMeters = fWorldHeightMeters;

	std::memcpy(dataSpan.data(), cpuHeightmapData.data(), iHeightmapDataSize);
}
