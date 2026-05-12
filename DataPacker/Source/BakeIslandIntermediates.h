#pragma once

// Pre-pass invoked from Main.cpp between ExportScene and ExportIsland.
// Drives Gaea 2 (Gaea.Swarm.exe) to bake per-island heightmap / color / AO / normal intermediates
// from `Island.json` + a resolved `.terrain` archetype, writing .r32 / .exr files that ExportIsland
// then consumes and block-compresses. Throws on any failure (caught by main()'s try/catch).
// Everything Gaea-2-specific is isolated in BakeIslandIntermediates.cpp for eventual Gaea 3 migration.
void BakeIslandIntermediates();

struct WorldDimensions
{
	float fFootprintMeters = 0.0f;   // Isotropic horizontal extent (Gaea Terrain.Width)
	float fElevationMeters = 0.0f;   // Total vertical span = kfOceanDepthMeters + max-above-water (Gaea Terrain.Height)
};

// Resolves the world-space dimensions for an island. Reads Island.json's optional
// widthMeters/elevationMeters override; falls back to the archetype `.terrain` file's intrinsic
// dimensions. Throws if Island.json is missing or the archetype can't be resolved.
WorldDimensions GetIslandDimensions(const std::filesystem::path& rIslandFolder);
