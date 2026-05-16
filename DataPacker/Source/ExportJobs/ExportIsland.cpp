#include "ExportIsland.h"

#include "BakeIslandIntermediates.h"
#include "Texture.h"

using enum common::ChunkFlags;

// ---------------------------------------------------------------------------
// Single-resolution island ingest
// ---------------------------------------------------------------------------

struct ExportedIsland
{
	std::vector<float> cpuHeightmapData;
	std::vector<float> cpuMeshPositions;   // float2 XY pairs in island-local meters (origin at center). Z is discarded — Terrain.vert re-derives it from the elevation sampler.
	std::vector<uint32_t> cpuMeshIndices;
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
	int32_t iMeshVertexCount = 0;
	int32_t iMeshIndexCount = 0;
	float fWorldFootprintXMeters = 0.0f;
	float fWorldFootprintYMeters = 0.0f;
	float fWorldElevationMeters = 0.0f;
};

static void ExportIslandData(const std::filesystem::path& rInputPath, ExportedIsland& rOut)
{
	std::filesystem::path intermediatesDir = rInputPath / kpcIslandIntermediatesDir;

	// BakedDimensions.json drives every downstream size. BakeIslandIntermediates writes it
	// before stamping the bake-version sentinel, so its presence is guaranteed if Gaea succeeded.
	// The AO file on disk is already cropped to (cropWidth × cropHeight); Color/Normals EXRs stay
	// full-res on disk (OpenEXR core writer is absent) and are cropped in-memory below before
	// BC encoding so the final outputs and JPG sidecars land at crop dims.
	BakedDimensions baked = ReadBakedDimensions(rInputPath);
	rOut.fWorldFootprintXMeters = baked.fWidthMeters;
	rOut.fWorldFootprintYMeters = baked.fHeightMeters;
	rOut.fWorldElevationMeters = baked.fElevationMeters;
	int64_t iElevationWidth = baked.iCropWidth / kiElevationDivisor;
	int64_t iElevationHeight = baked.iCropHeight / kiElevationDivisor;

	// Encode each intermediate as a single-mip texture in turn. sEncodeMutex serializes the BC
	// encoder across textures (it uses all hardware threads internally; mutex bounds memory). A
	// JPEG sidecar is written next to each output for visual diagnosis of the bake input.
	static constexpr int kiJpegSidecarQuality = 90;
	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "AmbientOcclusion.r16", FileType::kUint16Raw, false, baked.iCropWidth, baked.iCropHeight);
		texture.Save(rInputPath / kpcIslandAmbientOcclusion, VK_FORMAT_BC4_UNORM_BLOCK, false);
		texture.SaveJpegSidecar(rInputPath / "AmbientOcclusion.jpg", kiJpegSidecarQuality, true);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "Color.exr", FileType::kExr, true);
		texture.Crop(baked.iCropX, baked.iCropY, baked.iCropWidth, baked.iCropHeight);
		texture.Save(rInputPath / kpcIslandColor, VK_FORMAT_BC7_UNORM_BLOCK, true);
		texture.SaveJpegSidecar(rInputPath / "Color.jpg", kiJpegSidecarQuality, false);
	}

	{
		std::lock_guard<std::mutex> lock(Texture::sEncodeMutex);
		Texture texture(intermediatesDir / "Normals.exr", FileType::kExr, true);
		texture.Crop(baked.iCropX, baked.iCropY, baked.iCropWidth, baked.iCropHeight);
		texture.Save(rInputPath / kpcIslandNormals, VK_FORMAT_BC5_UNORM_BLOCK, false);
		texture.SaveJpegSidecar(rInputPath / "Normals.jpg", kiJpegSidecarQuality, false);
	}

	// CPU heightmap: float copy of the elevation pixels, packed into the chunk data payload for
	// runtime nav / world-Z queries. Source is the same Elevation.r32 BakeIslandIntermediates
	// pre-scaled to engine-meters using the global constant kfBeachHeightMeters as the beach offset
	// (beach = 0, ocean = negative down to -kfBeachHeightMeters, land = positive).
	rOut.cpuHeightmapData.resize(static_cast<size_t>(iElevationWidth) * static_cast<size_t>(iElevationHeight));
	{
		std::fstream rawStream(intermediatesDir / "Elevation.r32", std::ios::in | std::ios::binary);
		rawStream.read(reinterpret_cast<char*>(rOut.cpuHeightmapData.data()), rOut.cpuHeightmapData.size() * sizeof(float));
	}

	rOut.iHeightmapWidth = static_cast<int32_t>(iElevationWidth);
	rOut.iHeightmapHeight = static_cast<int32_t>(iElevationHeight);

	// Per-island mesh: BakeIslandIntermediates wrote MeshProcessed.bin with [int32 vertexCount,
	// int32 indexCount, float3 positions, uint32 indices]. Strip Z here — Terrain.vert re-derives
	// it from the elevation sampler — and pack float2 XY pairs into the chunk payload after the
	// heightmap floats (see Export() and IslandHeader in DataFile.h).
	{
		std::filesystem::path meshFile = intermediatesDir / "MeshProcessed.bin";
		std::ifstream meshStream(meshFile, std::ios::binary);
		meshStream.read(reinterpret_cast<char*>(&rOut.iMeshVertexCount), sizeof(int32_t));
		meshStream.read(reinterpret_cast<char*>(&rOut.iMeshIndexCount), sizeof(int32_t));
		std::vector<float> meshPositionsXYZ(static_cast<size_t>(rOut.iMeshVertexCount) * 3);
		rOut.cpuMeshPositions.resize(static_cast<size_t>(rOut.iMeshVertexCount) * 2);
		rOut.cpuMeshIndices.resize(static_cast<size_t>(rOut.iMeshIndexCount));
		meshStream.read(reinterpret_cast<char*>(meshPositionsXYZ.data()), static_cast<std::streamsize>(meshPositionsXYZ.size() * sizeof(float)));
		meshStream.read(reinterpret_cast<char*>(rOut.cpuMeshIndices.data()), static_cast<std::streamsize>(rOut.cpuMeshIndices.size() * sizeof(uint32_t)));
		for (int32_t i = 0; i < rOut.iMeshVertexCount; ++i)
		{
			rOut.cpuMeshPositions.at(static_cast<size_t>(i) * 2)     = meshPositionsXYZ.at(static_cast<size_t>(i) * 3);
			rOut.cpuMeshPositions.at(static_cast<size_t>(i) * 2 + 1) = meshPositionsXYZ.at(static_cast<size_t>(i) * 3 + 1);
		}
	}
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
	ExportedIsland exported;
	ExportIslandData(mInputPath, exported);

	std::filesystem::path relativeFile = mRelativeDirectory;
	relativeFile /= mInputPath.filename();

	int64_t iHeightmapDataSize = static_cast<int64_t>(exported.cpuHeightmapData.size() * sizeof(float));
	int64_t iMeshPositionBytes = static_cast<int64_t>(exported.cpuMeshPositions.size() * sizeof(float));
	int64_t iMeshIndexBytes = static_cast<int64_t>(exported.cpuMeshIndices.size() * sizeof(uint32_t));
	auto [pHeader, dataSpan] = AllocateHeaderAndData(iHeightmapDataSize + iMeshPositionBytes + iMeshIndexBytes);

	std::filesystem::path ambientOcclusionFile(relativeFile);
	ambientOcclusionFile /= kpcIslandAmbientOcclusion;
	pHeader->islandHeader.ambientOcclusionCrc = common::Crc(ambientOcclusionFile.string());

	std::filesystem::path colorsFile(relativeFile);
	colorsFile /= kpcIslandColor;
	pHeader->islandHeader.colorsCrc = common::Crc(colorsFile.string());

	std::filesystem::path normalsFile(relativeFile);
	normalsFile /= kpcIslandNormals;
	pHeader->islandHeader.normalsCrc = common::Crc(normalsFile.string());

	pHeader->islandHeader.iHeightmapWidth = exported.iHeightmapWidth;
	pHeader->islandHeader.iHeightmapHeight = exported.iHeightmapHeight;
	pHeader->islandHeader.fWorldFootprintXMeters = exported.fWorldFootprintXMeters;
	pHeader->islandHeader.fWorldFootprintYMeters = exported.fWorldFootprintYMeters;
	pHeader->islandHeader.fWorldElevationMeters = exported.fWorldElevationMeters;
	pHeader->islandHeader.iMeshVertexCount = exported.iMeshVertexCount;
	pHeader->islandHeader.iMeshIndexCount = exported.iMeshIndexCount;

	// Chunk payload: [heightmap floats][mesh positions][mesh indices]. Runtime IslandTerrain slices
	// these contiguously using IslandHeader's count fields.
	std::byte* pData = reinterpret_cast<std::byte*>(dataSpan.data());
	std::memcpy(pData, exported.cpuHeightmapData.data(), iHeightmapDataSize);
	std::memcpy(pData + iHeightmapDataSize, exported.cpuMeshPositions.data(), iMeshPositionBytes);
	std::memcpy(pData + iHeightmapDataSize + iMeshPositionBytes, exported.cpuMeshIndices.data(), iMeshIndexBytes);
}
