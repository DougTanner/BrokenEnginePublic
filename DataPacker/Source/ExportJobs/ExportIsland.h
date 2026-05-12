#pragma once

#include "ExportJob.h"

// Legacy island dimensions (pre-baked single-resolution islands like Islands/01/). The new
// per-mip island layout (Islands/02/ and beyond) derives sizes per-island from the loaded
// `mip<N>/Elevation.r32` files; `kiIslandSize` only governs the legacy ingest path.
inline constexpr int64_t kiIslandSize = 2 * 4096;

// Elevation max texel dimension = color (mips[0]) / kiElevationDivisor. Elevation is sampled by a
// vertex grid in Terrain.vert, so it doesn't need the per-texel resolution of color/normals. Used
// by both legacy ingest and the Gaea/per-mip path to drop unused elevation mips.
inline constexpr int64_t kiElevationDivisor = 4;

// First index in a halving mip chain at which elevation should start. Mips[0..start) carry only
// color/AO/normals; mips[start..end) carry all four. Caller must verify mips is non-empty.
// Templated so callers can pass either the int32 mips array (from Island.json) or int64 mip sizes
// (derived from on-disk intermediates) without an intermediate conversion vector.
template <typename T>
inline size_t IslandElevationStartIndex(const std::vector<T>& rMips)
{
	T iElevationMax = rMips.at(0) / static_cast<T>(kiElevationDivisor);
	for (size_t i = 0; i < rMips.size(); ++i)
	{
		if (rMips.at(i) <= iElevationMax)
		{
			return i;
		}
	}
	return rMips.size();
}

inline constexpr char kpcIslandAmbientOcclusion[] = "AmbientOcclusion.BC4_UNORM_BLOCK";
inline constexpr char kpcIslandColor[] = "Color.BC7_UNORM_BLOCK";
inline constexpr char kpcIslandElevation[] = "Elevation.R32_SFLOAT";
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

	virtual int64_t GetVersion() const override { return Version(12); }

protected:

	virtual void Export() override;
};
