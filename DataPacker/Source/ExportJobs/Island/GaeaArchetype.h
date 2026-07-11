#pragma once

#include "BakeIslandIntermediates.h" // WorldDimensions

// Gaea-2-specific archetype patching + executable resolution, split out of BakeIslandIntermediates.cpp
// so all Gaea.Swarm-version-specific logic lives in one translation unit for eventual Gaea 3 migration.
// This header is intentionally json-free (no tinygltf/nlohmann pull-in): the entry points parse their
// .terrain JSON internally. Island.json -> archetype resolution (ResolveTerrain) stays in
// BakeIslandIntermediates.cpp because its signature takes a parsed nlohmann::json.

// Resolves Gaea.Swarm.exe: GAEA2_PATH env var first, then the default install location. Throws if
// neither exists.
std::filesystem::path ResolveGaeaExecutable();

// Walks Terrain.Nodes for the Sea node and returns its Level (normalized [0,1] where Gaea places the
// water surface within the [0,1] elevation range). Returns kfGaeaSeaLevelDefault when the Level key is
// absent — Gaea omits it from the JSON at the editor default (~0.0995). Throws if no Sea node exists in
// the graph at all, since downstream elevation math depends on a known beach reference.
float ReadArchetypeSeaLevel(const std::filesystem::path& rTerrainFile);

// Patches the per-route archetype copy: Terrain.{Width,Height}, the Gaea Route node's Choice (the
// subdivision selector for this route), every per-node Seed (only when iSeed != 0; iSeed == 0 is the
// "use the archetype's per-node Seed values as authored" sentinel so you can A/B against Gaea's editor
// preview without DataPacker overwriting them), and (optionally) Mesher.VerticesPerSide. The Sea node's
// Level is intentionally NOT patched — it is read separately by ReadArchetypeSeaLevel and consumed by
// the post-bake elevation math so the Gaea editor preview and the in-game terrain agree on the water
// surface position. Each route works on its own cached PatchedArchetype.terrain copy, so the
// on-disk source archetype is never mutated.
void PatchArchetype(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions, int32_t iSeed, std::optional<int64_t> oiMeshResolution, int32_t iGaeaRouteChoice);
