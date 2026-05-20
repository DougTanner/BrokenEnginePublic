#pragma once

#include "ExportJob.h"

// Elevation max texel dimension = color (texturePixels) / kiElevationDivisor. Elevation is sampled
// by a vertex grid in Terrain.vert, so it doesn't need the per-texel resolution of color/normals.
// BakeIslandIntermediates downsamples Gaea's full-resolution elevation in-process to this ratio.
inline constexpr int64_t kiElevationDivisor = 4;

// Color / Normals / AO / Masks pixels whose corresponding downsampled heightmap sample sits below
// this depth (engine-meters; beach = 0, sea floor = -10) are deemed never visible and replaced
// with a per-format flat value before BC encoding so RDO + zlib gain large constant runs. 2.5 m
// of water column is well past the camera's effective underwater visibility; the band [-2.5, 0]
// is preserved so beach / shallow-water transitions stay artifact-free at the cut line.
inline constexpr float kfUnderwaterMaskThresholdMeters = -2.5f;

inline constexpr char kpcIslandIntermediatesDir[] = "Intermediates";

inline constexpr char kpcIslandAmbientOcclusion[] = "AmbientOcclusion.BC4_UNORM_BLOCK";
inline constexpr char kpcIslandColor[] = "Color.BC7_UNORM_BLOCK";
inline constexpr char kpcIslandMasks[] = "Masks.BC7_UNORM_BLOCK";
inline constexpr char kpcIslandNormals[] = "Normals.BC5_UNORM_BLOCK";

class ExportIsland : public ExportJob
{
public:

	static inline constexpr std::string_view kName = "Islands";

	static std::optional<common::ChunkFlags_t> Handles(const std::filesystem::directory_entry& rDirectoryEntry);

	ExportIsland(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportIsland() = default;

	virtual int64_t GetVersion() const override { return Version(25); }

	virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	virtual void Export() override;
};
