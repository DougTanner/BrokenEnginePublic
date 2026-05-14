#include "ExportIsland.h"

#include "BakeIslandIntermediates.h"
#include "Texture.h"

using enum common::ChunkFlags;

// Derive a square texture's side length from a headerless raw file's byte count.
template <typename T>
static int64_t DeriveSquareSize(const std::filesystem::path& rRawFile)
{
	int64_t iFileSize = std::filesystem::file_size(rRawFile);
	int64_t iSampleCount = iFileSize / static_cast<int64_t>(sizeof(T));
	int64_t iSize = static_cast<int64_t>(std::sqrt(static_cast<double>(iSampleCount)));
	ASSERT(iSize * iSize == iSampleCount);
	return iSize;
}

// ---------------------------------------------------------------------------
// Single-resolution island ingest
// ---------------------------------------------------------------------------

static void ExportIslandData(const std::filesystem::path& rInputPath, std::vector<float>& rCpuHeightmapData, int32_t& riHeightmapSize, float& rfWorldFootprintMeters, float& rfWorldElevationMeters)
{
	std::filesystem::path intermediatesDir = rInputPath / kpcIslandIntermediatesDir;

	int64_t iBaseSize = DeriveSquareSize<uint16_t>(intermediatesDir / "AmbientOcclusion.r16");
	int64_t iElevationSize = DeriveSquareSize<float>(intermediatesDir / "Elevation.r32");
	ASSERT(iElevationSize == iBaseSize / kiElevationDivisor);

	// World dimensions: read from Island.json's required widthMeters/elevationMeters.
	WorldDimensions worldDimensions = GetIslandDimensions(rInputPath);
	rfWorldFootprintMeters = worldDimensions.fFootprintMeters;
	rfWorldElevationMeters = worldDimensions.fElevationMeters;

	// Encode each intermediate as a single-mip texture in turn. sEncodeMutex serializes the BC
	// encoder across textures (it uses all hardware threads internally; mutex bounds memory). A
	// JPEG sidecar is written next to each output for visual diagnosis of the bake input.
	constexpr int kiJpegSidecarQuality = 90;
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "AmbientOcclusion.r16", FileType::kUint16Raw, false, iBaseSize, iBaseSize);
		texture.Save(rInputPath / kpcIslandAmbientOcclusion, VK_FORMAT_BC4_UNORM_BLOCK, false);
		texture.SaveJpegSidecar(rInputPath / "AmbientOcclusion.jpg", kiJpegSidecarQuality, true);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "Color.exr", FileType::kExr, true);
		texture.Save(rInputPath / kpcIslandColor, VK_FORMAT_BC7_UNORM_BLOCK, true);
		texture.SaveJpegSidecar(rInputPath / "Color.jpg", kiJpegSidecarQuality, false);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "Elevation.r32", FileType::kFloat32, false, iElevationSize, iElevationSize);
		texture.Save(rInputPath / kpcIslandElevation, VK_FORMAT_R32_SFLOAT, false);
		texture.SaveJpegSidecar(rInputPath / "Elevation.jpg", kiJpegSidecarQuality, true, true);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "Normals.exr", FileType::kExr, true);
		texture.Save(rInputPath / kpcIslandNormals, VK_FORMAT_BC5_UNORM_BLOCK, false);
		texture.SaveJpegSidecar(rInputPath / "Normals.jpg", kiJpegSidecarQuality, false);
	}

	// CPU heightmap: float copy of the elevation pixels, packed into the chunk data payload for
	// runtime nav / world-Z queries. Source is the same Elevation.r32 BakeIslandIntermediates
	// pre-scaled to engine-meters using the archetype's Sea node ShoreHeight as the beach offset
	// (beach = 0, ocean = negative down to -ShoreHeight × elevationMeters, land = positive).
	rCpuHeightmapData.resize(iElevationSize * iElevationSize);
	{
		std::fstream rawStream(intermediatesDir / "Elevation.r32", std::ios::in | std::ios::binary);
		rawStream.read(reinterpret_cast<char*>(rCpuHeightmapData.data()), rCpuHeightmapData.size() * sizeof(float));
	}

	riHeightmapSize = static_cast<int32_t>(iElevationSize);
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

	bool bHasIslandData = std::filesystem::exists(rDirectoryEntry.path() / kpcIslandIntermediatesDir / "AmbientOcclusion.r16");
	return bHasIslandData ? std::optional<common::ChunkFlags_t>(common::ChunkFlags::kIsland) : std::nullopt;
}

bool ExportIsland::CheckDirty(const std::filesystem::path& rPackFile)
{
	if (ExportJob::CheckDirty(rPackFile))
	{
		return true;
	}

	// Base CheckDirty compares against the island directory's mtime, which does NOT propagate from
	// edits inside Intermediates/ (where the Gaea pre-pass writes its outputs). Re-run the export
	// whenever any intermediate is newer than the chunk file so a fresh bake actually reaches the
	// .pack / .jpg sidecars.
	std::filesystem::file_time_type chunkFileLastWriteTime = std::filesystem::last_write_time(mChunkFile);
	std::filesystem::path intermediatesDir = mInputPath / kpcIslandIntermediatesDir;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(intermediatesDir))
	{
		if (rEntry.is_regular_file() && std::filesystem::last_write_time(rEntry.path()) > chunkFileLastWriteTime)
		{
			auto [date, time] = common::FileTimeString(std::filesystem::last_write_time(rEntry.path()));
			LOG(kDefault, kDebug, "Intermediate \"{}\" is newer than chunk \"{}\": {} {}", rEntry.path().string(), mChunkFile.string(), date, time);
			mbDirty = true;
			return mbDirty;
		}
	}

	return false;
}

void ExportIsland::Export()
{
	std::vector<float> cpuHeightmapData;
	int32_t iHeightmapSize = 0;
	float fWorldFootprintMeters = 0.0f;
	float fWorldElevationMeters = 0.0f;

	ExportIslandData(mInputPath, cpuHeightmapData, iHeightmapSize, fWorldFootprintMeters, fWorldElevationMeters);

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
