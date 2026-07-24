#pragma once

#include "ExportJob.h"

// Elevation max texel dimension = color (texturePixels) / kiElevationDivisor. Elevation is sampled
// by a vertex grid in Terrain.vert, so it doesn't need the per-texel resolution of color/normals.
// BakeIslandIntermediates downsamples Gaea's full-resolution elevation in-process to this ratio.
inline constexpr int64_t kiElevationDivisor = 4;

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

	// Governs the chunk payload only (heightmap R16 quantize, mesh XY, indices, valid-area hull) — the
	// BC texture encode is versioned separately by kiTextureVersion below, so a payload bump re-packs
	// chunks without rewriting the tracked island textures.
	virtual int64_t GetVersion() const override { return Version(29); }

	// Governs the four BC encodes (AmbientOcclusion / Color / Normals / Masks) only. Deliberately a
	// plain int64_t rather than Version(...): folding in sizeof(common::ChunkHeader) would retie the
	// encode to every unrelated chunk-header layout change, re-encoding and rewriting the ~446 MB of
	// Git-tracked island textures for a change that cannot alter a single texel.
	static constexpr int64_t kiTextureVersion = 1;

	virtual bool CheckDirty(const std::filesystem::path& rPackFile) override;

protected:

	virtual std::string GetInputFingerprint() const override;
	virtual void Export() override;
	virtual void UpdateCacheMetadata() override;
	virtual void CleanupOnFailure() override;

private:

	std::filesystem::path GetTextureMarkerPath() const;
	std::string GetTextureFingerprint() const;
	bool AreTextureOutputsPresent() const;
	bool AreTexturesFresh() const;
	void WriteTextureMarker() const;
};
