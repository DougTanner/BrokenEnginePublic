#pragma once

#include "ExportJob.h"

inline constexpr int64_t kiIslandSize = 2 * 4096;
inline constexpr int64_t kiAmbientOcclusionDivisor = 2;
inline constexpr int64_t kiElevationDivisor = 4;
inline constexpr int64_t kiNormalsDivisor = 1;

inline constexpr char kpcIslandAmbientOcclusion[] = "AmbientOcclusion.BC4_UNORM_BLOCK";
inline constexpr char kpcIslandColor[] = "Color.BC7_UNORM_BLOCK";
inline constexpr char kpcIslandElevation[] = "Elevation.R16_UNORM";
inline constexpr char kpcIslandNormals[] = "Normals.BC7_UNORM_BLOCK";

class ExportIsland : public ExportJob
{
public:

	ExportIsland(common::ChunkFlags_t rChunkFlags, const std::filesystem::path& rFile)
	: ExportJob(rChunkFlags, rFile)
	{
	}

	virtual ~ExportIsland() = default;

protected:

	virtual void PreExport();
	virtual void Export();
};
