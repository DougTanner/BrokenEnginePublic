#pragma once

// Private (non-public) header shared by the three BakeIslandIntermediates translation units —
// BakeIslandIntermediates.cpp (top-level orchestration), BakeRoute.cpp (one route's Gaea bake +
// split), and ProcessBakedRegion.cpp (per-region crop / leaf write). Holds the cross-TU bake
// context structs, the constants used by more than one of those TUs, and the forward declarations
// for the two cross-TU bake functions. NOT part of the public API (BakeIslandIntermediates.h);
// constants used by only one TU stay file-local in that TU.

#include "BakeIslandIntermediates.h"
#include "ExportJobs/ExportIsland.h"

// Auto-crop epsilon, measured in meters ABOVE THE SEA FLOOR (NOT above the beach line). A pixel
// is retained in the bbox when its elevation is strictly greater than `seaFloor + epsilon` —
// i.e., we trim only the deepest `kfCropEpsilonAboveSeaFloorMeters` of the water column. Sea
// floor is per-island at -(Level × elevationMeters), so for Level 0.1 + elevationMeters 100 m
// the cut line lives at -9 m, keeping the entire above-water landmass plus a halo of shallow
// water around the coastline.
constexpr float kfCropEpsilonAboveSeaFloorMeters = 1.0f;

// Minimum max downsampled terrain height (engine-meters above beach) for a leaf to be exported.
// Leaves peaking below this are very low / underwater nubs; ProcessBakedRegion deletes them so they
// produce no kIsland chunk (and no orphan texture chunks). The shipping islands separate cleanly at
// this value (nothing peaks in [4.44, 7.02) m anywhere; next-lowest kept leaf peaks ~7.02 m). Bump
// kiSplitVersion when changed.
constexpr float kfMinIslandMaxHeightMeters = 7.0f;

// Crop dimensions must satisfy BC block alignment (4) AND Vulkan transfer-queue granularity
// (block-relative for compressed formats; 16 blocks). The elevation path (downsampled by
// kiElevationDivisor) needs kiElevationDivisor × 16. max(...) picks the tighter; both are 4 today
// so this is 64. iTexturePixels must be a multiple of this; the auto-crop expands each chunk's bbox
// to a multiple of it, borrowing neighbour pixels across a split seam when the region edge isn't
// aligned, so per-chunk split spans need not themselves be aligned.
constexpr int64_t kiBcBlockSize = 4;
constexpr int64_t kiTransferGranularityBlocks = 16;
constexpr int64_t kiCropAlignment = (kiBcBlockSize > kiElevationDivisor ? kiBcBlockSize : kiElevationDivisor) * kiTransferGranularityBlocks;

// Single source of truth for required Island.json keys. Drives both the strict-presence check in
// BakeOne and the varsJson strip pass in BakeRoute (remaining keys forward to Gaea as graph
// variables). Adding a new DataPacker-owned key means appending here only.
constexpr const char* kpcRequiredIslandJsonKeys[] = {"archetype", "seed", "widthMeters", "elevationMeters", "texturePixels", "routes"};

// Route subdivision descriptor. `pcLabel` is BOTH the Island.json "routes" value AND the per-route
// sub-folder name; `iGaeaChoice` is the 0-based Gaea Route node input-port index; `iColumns` /
// `iRows` split the full square bake along X (east-west) / Y (north-south). See the kRouteSubdivisions
// table in BakeIslandIntermediates.cpp for the full routing semantics.
struct RouteSubdivision
{
	const char* pcLabel;
	int32_t iGaeaChoice;
	int64_t iColumns;
	int64_t iRows;
};

// Per-island bake context: everything constant for one Island.json across all of its routes.
// Built once at the top of BakeOne and threaded into every BakeRoute / ProcessBakedRegion call.
// All members are caller-owned references; the context outlives its consumers.
struct IslandBakeContext
{
	const std::filesystem::path& rGaeaExecutable;
	const std::filesystem::path& rIslandFolder;
	const std::filesystem::path& rCacheIslandFolder;
	const std::filesystem::path& rArchetypeFile;
	const std::filesystem::path& rIslandJsonFile;
	const nlohmann::json& rIslandJson;
	const WorldDimensions& rDimensions;
	int32_t iSeed;
	int64_t iTexturePixels;
	std::optional<int64_t> oiMeshResolution;
};

// Per-region bounds passed to ProcessBakedRegion. Half-open ranges in full-bake pixel coords.
struct RegionBounds
{
	int64_t iStartX;
	int64_t iEndX;
	int64_t iStartY;
	int64_t iEndY;
};

// Per-region output target. The source leaf preserves the authored BC intermediates and provides
// ExportIsland's chunk identity; the cache leaf owns all derived bake data.
struct LeafTarget
{
	const std::filesystem::path& rSourceLeafDirectory;
	const std::filesystem::path& rCacheLeafDirectory;
};

// Per-route bake outputs produced once after Gaea runs, then consumed by every per-region call.
// All buffers are caller-owned; lifetime exceeds ProcessBakedRegion's call.
struct BakeOutput
{
	const std::vector<float>& rFullElevationMeters;
	const std::vector<uint16_t>& rFullAmbientOcclusion;
	float fBeachOffsetMeters;
};

// Crops one chunk region out of the full Gaea bake and writes the chunk's per-region geometry into
// the leaf's Gaea cache folder; returns false if the chunk is rejected as too low. Defined in
// ProcessBakedRegion.cpp; full contract documented at that definition.
bool ProcessBakedRegion(const IslandBakeContext& rContext, const BakeOutput& rBakeOutput, const RegionBounds& rRegion, std::vector<float> meshPositions, std::vector<uint32_t> meshIndices, const LeafTarget& rLeaf);

// Bakes one route of one island in two stages (Gaea raw export, then split into chunk leaves via
// ProcessBakedRegion). Defined in BakeRoute.cpp; full contract documented at that definition.
void BakeRoute(const IslandBakeContext& rContext, const RouteSubdivision& rRoute);
