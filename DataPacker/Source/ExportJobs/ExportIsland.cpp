#include "ExportIsland.h"

#include "Texture.h"

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

using enum common::ChunkFlags;

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

std::optional<common::ChunkFlags_t> ExportIsland::Handles(const std::filesystem::directory_entry& rDirectoryEntry)
{
	if (!rDirectoryEntry.is_directory() || rDirectoryEntry.path().parent_path().filename() != "Islands")
	{
		return std::nullopt;
	}

	// Elevation is required for the beach-elevation computation; skip folders that haven't been baked yet (e.g., pre-Gaea work-in-progress).
	const std::filesystem::path& rPath = rDirectoryEntry.path();
	bool bHasElevation = std::filesystem::exists(rPath / "Elevation.r32") || std::filesystem::exists(rPath / kpcIslandElevation);
	return bHasElevation ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kIsland) : std::nullopt;
}

void ExportIsland::Export()
{
	std::filesystem::path ambientOcclusionFloat32File(mInputPath);
	ambientOcclusionFloat32File /= "AmbientOcclusion.r32";
	if (std::filesystem::exists(ambientOcclusionFloat32File))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(ambientOcclusionFloat32File, FileType::kFloat32, true, kiIslandSize, kiIslandSize);
		texture.Downsize(1);

		std::filesystem::path path(mInputPath);
		path /= kpcIslandAmbientOcclusion;
		texture.Save(path, VK_FORMAT_BC4_UNORM_BLOCK, false);

		std::filesystem::remove(ambientOcclusionFloat32File);
	}

	std::filesystem::path colorExrFile(mInputPath);
	colorExrFile /= "Color.exr";
	if (std::filesystem::exists(colorExrFile))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(colorExrFile, FileType::kExr, true);
		std::filesystem::path path(mInputPath);
		path /= kpcIslandColor;
		texture.Save(path, VK_FORMAT_BC7_UNORM_BLOCK, true);

		std::filesystem::remove(colorExrFile);
	}

	// CPU heightmap data to be exported in chunk
	std::vector<float> cpuHeightmapData;
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;

	std::filesystem::path elevationFloat32File(mInputPath);
	elevationFloat32File /= "Elevation.r32";
	if (std::filesystem::exists(elevationFloat32File))
	{
		// Determine source dimensions from file size (assuming square texture)
		int64_t iElevationFileSize = std::filesystem::file_size(elevationFloat32File);
		int64_t iSourceSize = static_cast<int64_t>(std::sqrt(static_cast<double>(iElevationFileSize) / sizeof(float)));
		ASSERT(iSourceSize * iSourceSize * static_cast<int64_t>(sizeof(float)) == iElevationFileSize);

		// CPU heightmap export - downsample and extract float data
		{
			Texture cpuTexture(elevationFloat32File, FileType::kFloat32, false, iSourceSize, iSourceSize);
			cpuTexture.Downsize(2);

			iHeightmapWidth = static_cast<int32_t>(cpuTexture.miWidth);
			iHeightmapHeight = static_cast<int32_t>(cpuTexture.miHeight);
			cpuHeightmapData = cpuTexture.mData.at(0);
		}

		// GPU texture export - downsample and convert to R16_UNORM
		{
			std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
			Texture gpuTexture(elevationFloat32File, FileType::kFloat32, false, iSourceSize, iSourceSize);
			gpuTexture.Downsize(2);

			std::filesystem::path path(mInputPath);
			path /= kpcIslandElevation;
			gpuTexture.Save(path, VK_FORMAT_R16_UNORM, false);
		}

		std::filesystem::remove(elevationFloat32File);
	}

	// Fallback: Load from already-processed R16_UNORM file if r32 didn't exist
	if (cpuHeightmapData.empty())
	{
		std::filesystem::path elevationR16File(mInputPath);
		elevationR16File /= kpcIslandElevation;

		if (std::filesystem::exists(elevationR16File))
		{
			int64_t iWidth = 0;
			int64_t iHeight = 0;
			int64_t iMipMaps = 0;
			std::vector<std::byte> data = LoadIntermediate(elevationR16File, iWidth, iHeight, iMipMaps, VK_FORMAT_R16_UNORM);

			int64_t iPixelCount = iWidth * iHeight;
			const uint16_t* puiR16 = reinterpret_cast<const uint16_t*>(data.data());

			// Convert to float
			cpuHeightmapData.resize(iPixelCount);
			for (int64_t i = 0; i < iPixelCount; ++i)
			{
				cpuHeightmapData.at(i) = common::UnormToFloat<uint16_t>(puiR16[i]);
			}

			iHeightmapWidth = static_cast<int32_t>(iWidth);
			iHeightmapHeight = static_cast<int32_t>(iHeight);
		}
	}

	std::filesystem::path normalsExrFile(mInputPath);
	normalsExrFile /= "Normals.exr";
	if (std::filesystem::exists(normalsExrFile))
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(normalsExrFile, FileType::kExr, true);

		std::filesystem::path path(mInputPath);
		path /= kpcIslandNormals;
		texture.Save(path, VK_FORMAT_BC5_UNORM_BLOCK, false);

		std::filesystem::remove(normalsExrFile);
	}
	else
	{
		// One-time migration: pre-baked islands shipped before BC5 support carry "Normals.BC7_UNORM_BLOCK".
		// Decode mip 0, drop the BC7 file, and re-encode through the standard BC5 mip pipeline.
		std::filesystem::path bc5NormalsFile(mInputPath);
		bc5NormalsFile /= kpcIslandNormals;
		std::filesystem::path bc7NormalsFile(mInputPath);
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
	std::filesystem::path elevationU16File(mInputPath);
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
	uint16_t uiBeachElevation = 0;
	for (const auto& [ruiElevation, riCount] : map)
	{
		if (ruiElevation != 0 && riCount > iMaxCount)
		{
			iMaxCount = riCount;
			uiBeachElevation = ruiElevation;
		}
	}
	LOG(kDefault, kDebug, "Beach elevation: {} {} ({} times)", uiBeachElevation, common::UnormToFloat(uiBeachElevation), iMaxCount);

	// Save crcs, beach elevation, and CPU heightmap
	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	int64_t iHeightmapDataSize = cpuHeightmapData.size() * sizeof(float);
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

	// Write CPU heightmap data
	std::memcpy(dataSpan.data(), cpuHeightmapData.data(), iHeightmapDataSize);
}
