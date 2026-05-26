#include "BakeIslandIntermediates.h"

#include "FileManager.h"
#include "SubdivideBeachBand.h"
#include "ExportJobs/ExportIsland.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#if defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "tinygltf/json.hpp"
#include "tinygltf/tiny_gltf.h"
#if defined(__clang__)
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

namespace
{

constexpr const wchar_t* kpwcGaeaDefaultPath = L"C:\\Program Files\\QuadSpinner\\Gaea 2\\Gaea.Swarm.exe";
constexpr const char* kpcGaeaEnvVar = "GAEA2_PATH";

// One Intermediates folder per route (folder name kpcIslandIntermediatesDir, see ExportIsland.h).
// One Gaea bake per route produces all six files below at texturePixels resolution; the raw
// Elevation.r32 stays as the bake source (it is NOT rewritten in place) — ProcessBakedRegion reads
// it and writes the per-chunk downsampled (texturePixels / kiElevationDivisor) elevation into each
// leaf's own Intermediates/. Elevation.r32 is headerless IEEE-754 float (Gaea's FloatRaw32 format),
// normalized [0,1] at bake time — DataPacker reads the Sea node Level from the archetype (fallback
// kfGaeaSeaLevelDefault when absent), then scales to meters by elevationMeters and subtracts
// `Level × elevationMeters` so beach = 0 in engine space and the sea floor sits at
// -(Level × elevationMeters) per island. AmbientOcclusion.r16 stays UshortRaw16 (precision-matched
// to BC4_UNORM). Color is 8-bit PNG (sRGB-encoded display data; downstream BC7 is 8-bit anyway, so
// EXR's float precision was wasted and its color-space convention conflicts with Gaea writing
// sRGB-encoded values into the EXR container). Normals stay multi-channel EXR. Files written by
// Gaea directly. Used for both the post-Gaea existence verification and the IsGaeaRawDirty
// existence check. These raw outputs live in each route's <route>/Intermediates/ folder.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.png",
	"Elevation.r32",
	"Normals.exr",
	"Mesh.gltf",  // Gaea Mesher output: glTF JSON manifest (separate-format)
	"Mesh.bin",   // Gaea Mesher output: binary buffer referenced by Mesh.gltf
};

// MeshProcessed.bin (positions + indices in island-local meters, XY-centered, sea-level Z=0) is
// derived per chunk leaf, not in the route's raw Intermediates — the per-leaf dirty check in
// AreLeavesDirty verifies it (and Elevation.r32 / AmbientOcclusion.r16 / BakedDimensions.json)
// exists in each chunk folder.

// Two-stage version sentinels, both route-level in <route>/Intermediates/. The bake separates the
// SLOW Gaea raw export from the FAST post-Gaea split so a split-only change (a kRouteSubdivisions
// columns/rows edit, or ProcessBakedRegion crop logic) re-splits from the existing Gaea output
// WITHOUT re-running Gaea.Swarm.
//
// kiBakeVersion (BakeVersion.txt): the Gaea RAW output. Bump only when something that changes the
// raw bake changes — the archetype patch (dims / seed / Route Choice / Mesher resolution) or the
// Gaea invocation. IsGaeaRawDirty re-runs Gaea on mismatch.
//
// kiSplitVersion (SplitVersion.txt): the post-Gaea split. Bump when ProcessBakedRegion or the chunk
// split (incl. kRouteSubdivisions columns/rows) changes. AreLeavesDirty re-splits from the existing
// raw on mismatch — no Gaea re-export.
constexpr int32_t kiBakeVersion = 27;
constexpr int32_t kiSplitVersion = 1;

// Fallback assumption for the Gaea Sea node's normalized `Level` field. Gaea omits the key
// from .terrain JSON when it equals the editor default (~0.0995); we treat the missing case as
// 0.1 so DataPacker matches what the Gaea editor preview shows. Archetypes that author Level
// explicitly override this fallback. DataPacker never patches Level — it is read once per bake
// in BakeOne via ReadArchetypeSeaLevel and multiplied by elevationMeters to derive the per-island
// beach offset (engine-Z 0 == beach; sea floor sits at -(Level × elevationMeters)).
constexpr float kfGaeaSeaLevelDefault = 0.1f;

// Beach-band adaptive subdivision constants. After the Gaea Mesher mesh is parsed, every triangle
// whose Z-range overlaps the beach band gets recursively split (1->4 midpoint) until its longest XY
// edge falls below the target. Out-of-band neighbors that inherit a midpoint via a shared edge get
// the minimal absorption split (1->2 for 1 midpoint, 1->3 for 2, true 1->4 for 3) -- introducing no
// new midpoints, so the cascade dies at one ring around the band. See plan
// `Documents/Plans/Graphics/BeachMeshSubdivide.md`.
// Band straddles beach (engine-Z = 0): triangles whose Z-range overlaps
// [kfBeachSubdivisionMinMeters, kfBeachSubdivisionMaxMeters] in absolute engine-meters densify,
// so both shallow water and just-above-beach terrain are covered. Independent of elevationMeters;
// set the two bounds asymmetrically to widen the underwater or above-water side independently.
constexpr float kfBeachSubdivisionMinMeters = -0.25f;
constexpr float kfBeachSubdivisionMaxMeters = 0.5f;
constexpr float kfBeachSubdivisionMaxEdgeMeters = 1.0f;
constexpr int32_t kiBeachSubdivisionMaxDepth = 12;

// Auto-crop epsilon, measured in meters ABOVE THE SEA FLOOR (NOT above the beach line). A pixel
// is retained in the bbox when its elevation is strictly greater than `seaFloor + epsilon` —
// i.e., we trim only the deepest `kfCropEpsilonAboveSeaFloorMeters` of the water column. Sea
// floor is per-island at -(Level × elevationMeters), so for Level 0.1 + elevationMeters 100 m
// the cut line lives at -9 m, keeping the entire above-water landmass plus a halo of shallow
// water around the coastline.
constexpr float kfCropEpsilonAboveSeaFloorMeters = 1.0f;
constexpr const char* kpcBakeVersionFile = "BakeVersion.txt";    // Gaea-raw stage sentinel (kiBakeVersion)
constexpr const char* kpcSplitVersionFile = "SplitVersion.txt";  // post-Gaea split stage sentinel (kiSplitVersion)
constexpr const char* kpcPatchedArchetypeFile = "PatchedArchetype.terrain";

// Crop dimensions must satisfy BC block alignment (4) AND Vulkan transfer-queue granularity
// (block-relative for compressed formats; 16 blocks). The elevation path (downsampled by
// kiElevationDivisor) needs kiElevationDivisor × 16. max(...) picks the tighter; both are 4 today
// so this is 64. iTexturePixels (and, for split routes, iTexturePixels / columns) must be a
// multiple of this so the worst-case bbox-near-edge expansion can't overflow the region.
constexpr int64_t kiBcBlockSize = 4;
constexpr int64_t kiTransferGranularityBlocks = 16;
constexpr int64_t kiCropAlignment = (kiBcBlockSize > kiElevationDivisor ? kiBcBlockSize : kiElevationDivisor) * kiTransferGranularityBlocks;

// Route → subdivision table (single source of truth). `pcLabel` is BOTH the Island.json "routes"
// value AND the per-route sub-folder name. `iGaeaChoice` is patched into the archetype's single
// Gaea Route node ("Choice"): 0 = 1x1 (single landmass), 1 = 2x1 (dual band), 2 = 2x2 (quad grid).
// `iColumns`/`iRows` split the full square bake into that many chunks along X (east-west) /
// Y (north-south); each chunk becomes an independent kIsland in the pack.
//
// NOTE: the split axes must match how the Route lays out its landmasses. The "2x1" dual band stacks
// two landmasses along Y (Route Input2 feeds two copies of the cone-edge offset by ±OffsetY), so it
// splits along Y only (iRows = 2). The "2x2" quad grid offsets four copies by ±OffsetX AND ±OffsetY
// into the four quadrant centers, so it splits along both axes (iColumns = iRows = 2) to cut BETWEEN
// the landmasses rather than through them. The label names the route, not the pixel axis. Editing
// iColumns/iRows is a post-Gaea split change: bump kiSplitVersion (NOT kiBakeVersion) so existing
// Gaea bakes are reused and only the split re-runs.
struct RouteSubdivision
{
	const char* pcLabel;
	int32_t iGaeaChoice;
	int64_t iColumns;
	int64_t iRows;
};

constexpr RouteSubdivision kRouteSubdivisions[] =
{
	{"1x1", 0, 1, 1},
	{"2x1", 1, 1, 2},
	{"2x2", 2, 2, 2},
};

const RouteSubdivision& LookupRouteSubdivision(const std::string& rLabel, const std::filesystem::path& rIslandJsonFile)
{
	for (const RouteSubdivision& rRoute : kRouteSubdivisions)
	{
		if (rLabel == rRoute.pcLabel)
		{
			return rRoute;
		}
	}

	std::string validLabels;
	for (const RouteSubdivision& rRoute : kRouteSubdivisions)
	{
		if (!validLabels.empty())
		{
			validLabels += ", ";
		}
		validLabels += rRoute.pcLabel;
	}
	throw std::runtime_error(std::format("\"{}\" lists unknown route \"{}\". Valid routes: {}.", rIslandJsonFile.string(), rLabel, validLabels));
}

std::filesystem::path ResolveGaeaExecutable()
{
	char* pcEnvValue = nullptr;
	size_t uiEnvSize = 0;
	_dupenv_s(&pcEnvValue, &uiEnvSize, kpcGaeaEnvVar);
	if (pcEnvValue != nullptr)
	{
		std::filesystem::path envPath(pcEnvValue);
		free(pcEnvValue);
		if (std::filesystem::exists(envPath))
		{
			return envPath;
		}
	}

	std::filesystem::path defaultPath(kpwcGaeaDefaultPath);
	if (std::filesystem::exists(defaultPath))
	{
		return defaultPath;
	}

	throw std::runtime_error(std::format("Gaea.Swarm.exe not found. Set {} env var or install Gaea 2 to the default location ({}).", kpcGaeaEnvVar, std::filesystem::path(kpwcGaeaDefaultPath).string()));
}

std::filesystem::path ResolveTerrain(const std::filesystem::path& rIslandFolder, const nlohmann::json& rIslandJson)
{
	// Named archetype: two-tier lookup — any input dir's shared Islands/ folder, then sibling in
	// the island folder. Iterates all input directories so archetypes can live anywhere on the
	// search path.
	std::string archetype = rIslandJson.at("archetype").get<std::string>();

	for (const std::filesystem::path& rInputDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path sharedPath = rInputDirectory / "Islands" / (archetype + ".terrain");
		if (std::filesystem::exists(sharedPath))
		{
			return sharedPath;
		}
	}

	std::filesystem::path siblingPath = rIslandFolder / (archetype + ".terrain");
	if (std::filesystem::exists(siblingPath))
	{
		return siblingPath;
	}

	throw std::runtime_error(std::format("Archetype '{}' not found in any input directory's Islands/ folder or as a sibling of \"{}\".", archetype, rIslandFolder.string()));
}

// True if the route's RAW Gaea outputs are missing or stale — forces a (slow) Gaea.Swarm re-export.
// Checks only Gaea-output concerns: the raw intermediate files + the patched archetype (needed for
// the split's sea-level read) are present, the Gaea-bake-version sentinel matches, and the raw
// files are no older than Island.json / the archetype. The post-Gaea split is checked separately
// by AreLeavesDirty so a split-only change never trips this.
bool IsGaeaRawDirty(const std::filesystem::path& rIntermediatesDir, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile)
{
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rIntermediatesDir / pcFile))
		{
			return true;
		}
	}
	if (!std::filesystem::exists(rIntermediatesDir / kpcPatchedArchetypeFile))
	{
		return true;
	}

	std::filesystem::path versionFile = rIntermediatesDir / kpcBakeVersionFile;
	if (!std::filesystem::exists(versionFile))
	{
		return true;
	}
	int32_t iFileVersion = 0;
	{
		std::ifstream versionStream(versionFile);
		versionStream >> iFileVersion;
	}
	if (iFileVersion != kiBakeVersion)
	{
		return true;
	}

	std::filesystem::file_time_type inputNewest = std::max(std::filesystem::last_write_time(rIslandJsonFile), std::filesystem::last_write_time(rArchetypeFile));
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (std::filesystem::last_write_time(rIntermediatesDir / pcFile) < inputNewest)
		{
			return true;
		}
	}

	return false;
}

// True if any chunk leaf's derived split outputs are missing or stale — forces a re-split from the
// (assumed fresh) raw Gaea output, NOT a Gaea re-export. Checks the split-version sentinel (catches
// a kRouteSubdivisions columns/rows or ProcessBakedRegion change) and every leaf's per-region files.
bool AreLeavesDirty(const std::filesystem::path& rRouteDir, int64_t iLeafCount)
{
	std::filesystem::path splitVersionFile = rRouteDir / kpcIslandIntermediatesDir / kpcSplitVersionFile;
	if (!std::filesystem::exists(splitVersionFile))
	{
		return true;
	}
	int32_t iFileVersion = 0;
	{
		std::ifstream versionStream(splitVersionFile);
		versionStream >> iFileVersion;
	}
	if (iFileVersion != kiSplitVersion)
	{
		return true;
	}

	for (int64_t iLeaf = 0; iLeaf < iLeafCount; ++iLeaf)
	{
		std::filesystem::path leafIntermediatesDir = rRouteDir / std::to_string(iLeaf) / kpcIslandIntermediatesDir;
		if (!std::filesystem::exists(leafIntermediatesDir / kpcBakedDimensionsFile)
			|| !std::filesystem::exists(leafIntermediatesDir / "MeshProcessed.bin")
			|| !std::filesystem::exists(leafIntermediatesDir / "Elevation.r32")
			|| !std::filesystem::exists(leafIntermediatesDir / "AmbientOcclusion.r16"))
		{
			return true;
		}
	}

	return false;
}

void WriteFileBytes(const std::filesystem::path& rFile, const std::string& rBytes)
{
	// Atomic replace: partial write on crash leaves a stray .tmp, not a half-written archetype.
	// PID suffix so concurrent crashes from peer DataPacker processes leave distinct orphan
	// .<pid>.tmp files instead of clobbering each other's in-flight writes.
	std::filesystem::path tempFile = rFile;
	tempFile += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	{
		std::ofstream stream(tempFile, std::ios::binary);
		stream.write(rBytes.data(), rBytes.size());
	}
	std::filesystem::rename(tempFile, rFile);
}

// Recursive walker: every object whose key is exactly "Seed" with a numeric value is overwritten
// with iSeed. Gaea's `--seed` CLI flag only mixes a global seed into per-node randomness; it
// doesn't override the per-node "Seed" fields baked into the .terrain JSON. Patching them
// directly is the only way to make Island.json's seed value fully determine the bake.
void PatchArchetypeSeeds(nlohmann::json& rJson, int32_t iSeed)
{
	if (rJson.is_object())
	{
		for (auto& [rKey, rValue] : rJson.items())
		{
			if (rKey == "Seed" && rValue.is_number())
			{
				rValue = iSeed;
			}
			else
			{
				PatchArchetypeSeeds(rValue, iSeed);
			}
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeSeeds(rChild, iSeed);
		}
	}
}

// Recursive walker: every node whose $type contains "Mesher" gets its VerticesPerSide set. Gaea's
// default Mesher resolution is implicit (inherits BakeResolution) so the property may be absent
// from the JSON — we create the key when missing. Restored via the standard archetype-bytes rollback.
void PatchArchetypeMesherResolution(nlohmann::json& rJson, int64_t iVerticesPerSide)
{
	if (rJson.is_object())
	{
		auto it = rJson.find("$type");
		if (it != rJson.end() && it->is_string() && it->get<std::string>().find("Mesher") != std::string::npos)
		{
			rJson["VerticesPerSide"] = iVerticesPerSide;
		}
		for (auto& [rKey, rValue] : rJson.items())
		{
			PatchArchetypeMesherResolution(rValue, iVerticesPerSide);
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeMesherResolution(rChild, iVerticesPerSide);
		}
	}
}

// Recursive walker: finds the single node whose $type names a Gaea Route node and sets its
// "Choice" (0 = 1x1 single landmass, 1 = 2x1 dual band, 2 = 2x2 quad grid — see
// kRouteSubdivisions). The on-disk
// archetype authors Choice at whatever the editor was last saved at; DataPacker always patches it
// per route so the bake is deterministic. riRouteNodeCount accumulates matches so the caller can
// assert exactly one Route node exists.
void PatchArchetypeRoute(nlohmann::json& rJson, int32_t iChoice, int64_t& riRouteNodeCount)
{
	if (rJson.is_object())
	{
		auto it = rJson.find("$type");
		if (it != rJson.end() && it->is_string() && it->get<std::string>().starts_with("QuadSpinner.Gaea.Nodes.Route,"))
		{
			rJson["Choice"] = iChoice;
			++riRouteNodeCount;
		}
		for (auto& [rKey, rValue] : rJson.items())
		{
			PatchArchetypeRoute(rValue, iChoice, riRouteNodeCount);
		}
	}
	else if (rJson.is_array())
	{
		for (nlohmann::json& rChild : rJson)
		{
			PatchArchetypeRoute(rChild, iChoice, riRouteNodeCount);
		}
	}
}

// Walks Terrain.Nodes for the Sea node and returns its Level (normalized [0,1] where Gaea places
// the water surface within the [0,1] elevation range). Returns kfGaeaSeaLevelDefault when the
// Level key is absent — Gaea omits it from the JSON at the editor default (~0.0995). Throws if no
// Sea node exists in the graph at all, since downstream elevation math depends on a known beach
// reference. Nodes is a dict keyed by node ID, so we scan values for the one whose $type starts
// with the Gaea Sea node prefix.
float ReadArchetypeSeaLevel(const std::filesystem::path& rTerrainFile)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	const nlohmann::json& rNodes = terrainJson.at("Assets").at("$values").at(0).at("Terrain").at("Nodes");
	for (const auto& [rKey, rNode] : rNodes.items())
	{
		if (!rNode.is_object() || !rNode.contains("$type"))
		{
			continue;
		}
		std::string type = rNode.at("$type").get<std::string>();
		if (type.starts_with("QuadSpinner.Gaea.Nodes.Sea"))
		{
			return rNode.value("Level", kfGaeaSeaLevelDefault);
		}
	}
	throw std::runtime_error("Archetype has no Sea node — DataPacker reads its Level to derive the per-island beach offset. Add a Sea node to the graph or extend BakeIslandIntermediates to handle sea-less archetypes.");
}

// Patches the per-route archetype copy: Terrain.{Width,Height}, the Gaea Route node's Choice (the
// subdivision selector for this route), every per-node Seed (only when iSeed != 0; iSeed == 0 is
// the "use the archetype's per-node Seed values as authored" sentinel so you can A/B against
// Gaea's editor preview without DataPacker overwriting them), and (optionally)
// Mesher.VerticesPerSide. The Sea node's Level is intentionally NOT patched — it is read
// separately by ReadArchetypeSeaLevel and consumed by the post-bake elevation math so the Gaea
// editor preview and the in-game terrain agree on the water surface position. Each route works on
// its own Intermediates/PatchedArchetype.terrain copy, so the on-disk source archetype is never
// mutated.
void PatchArchetype(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions, int32_t iSeed, std::optional<int64_t> oiMeshResolution, int32_t iGaeaRouteChoice)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	rTerrain.at("Width") = rDimensions.fFootprintMeters;
	rTerrain.at("Height") = rDimensions.fElevationMeters;

	int64_t iRouteNodeCount = 0;
	PatchArchetypeRoute(terrainJson, iGaeaRouteChoice, iRouteNodeCount);
	if (iRouteNodeCount != 1)
	{
		throw std::runtime_error(std::format("Archetype \"{}\" has {} Gaea Route node(s); DataPacker expects exactly 1 to set the per-route subdivision Choice. Add a Route node to the graph (Choice 0 = 1x1, 1 = 2x1) or extend BakeIslandIntermediates for multi-Route archetypes.", rTerrainFile.string(), iRouteNodeCount));
	}

	if (iSeed != 0)
	{
		PatchArchetypeSeeds(terrainJson, iSeed);
	}

	if (oiMeshResolution.has_value())
	{
		PatchArchetypeMesherResolution(terrainJson, *oiMeshResolution);
	}

	WriteFileBytes(rTerrainFile, terrainJson.dump(2));
}

// Single source of truth for required Island.json keys. Drives both the strict-presence check in
// BakeOne and the varsJson strip pass in BakeRoute (remaining keys forward to Gaea as graph
// variables). Adding a new DataPacker-owned key means appending here only.
constexpr const char* kpcRequiredIslandJsonKeys[] = {"archetype", "seed", "widthMeters", "elevationMeters", "texturePixels", "routes"};

// Crops one chunk region [xStart,xEnd) x [yStart,yEnd) out of the full Gaea bake and writes the
// chunk's per-region geometry (Elevation.r32, AmbientOcclusion.r16, MeshProcessed.bin,
// BakedDimensions.json) into the leaf's own Intermediates/ folder (rLeafDir/Intermediates) — kept
// under "Intermediates" so the same .gitignore rule that ignores the raw route bake ignores these
// derived per-chunk files too; the committed BC outputs land at the leaf root later (ExportIsland).
// The shared full-res Color/Normals/mask PNGs are NOT copied — rTextureSourceDirRelative
// (leaf-relative, "../Intermediates") points ExportIsland at the route's Intermediates folder, and
// the crop rect recorded in BakedDimensions lets it crop them in-memory. A 1x1 route passes the
// whole texture as the region; a 2x1 route calls this once per Y-half, a 2x2 route once per quadrant
// (both axes split at the midpoints), so the existing auto-crop runs (and, in ExportIsland, the
// underwater re-coloring) once per chunk.
// meshPositions / meshIndices are taken by value so each region mutates its own copy of the shared
// post-subdivision mesh.
void ProcessBakedRegion(const std::vector<float>& rFullElevationMeters, const std::vector<uint16_t>& rFullAmbientOcclusion, std::vector<float> meshPositions, std::vector<uint32_t> meshIndices, int64_t iTexturePixels, const WorldDimensions& rDimensions, int64_t iRegionStartX, int64_t iRegionEndX, int64_t iRegionStartY, int64_t iRegionEndY, float fBeachOffsetMeters, const std::filesystem::path& rLeafDir, const std::string& rTextureSourceDirRelative)
{
	std::filesystem::path leafIntermediatesDir = rLeafDir / kpcIslandIntermediatesDir;
	std::filesystem::create_directories(leafIntermediatesDir);

	// Auto-crop to the bbox of pixels above the sea-floor cut line, restricted to this region so a
	// 2x1 half never pulls land across the split seam. Cut line = -fBeachOffsetMeters +
	// kfCropEpsilonAboveSeaFloorMeters (epsilon meters up from the per-island sea floor). Empty
	// bbox is a configuration error: the route produced no terrain in this chunk.
	float fCropCutLineMeters = -fBeachOffsetMeters + kfCropEpsilonAboveSeaFloorMeters;
	int64_t iMinX = iRegionEndX;
	int64_t iMinY = iRegionEndY;
	int64_t iMaxX = -1;
	int64_t iMaxY = -1;
	for (int64_t iY = iRegionStartY; iY < iRegionEndY; ++iY)
	{
		const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(iY) * static_cast<size_t>(iTexturePixels));
		for (int64_t iX = iRegionStartX; iX < iRegionEndX; ++iX)
		{
			if (pfRow[iX] > fCropCutLineMeters)
			{
				iMinX = std::min(iMinX, iX);
				iMaxX = std::max(iMaxX, iX);
				iMinY = std::min(iMinY, iY);
				iMaxY = std::max(iMaxY, iY);
			}
		}
	}
	if (iMaxX < 0)
	{
		throw std::runtime_error(std::format("Island chunk \"{}\" region [{}..{}, {}..{}] has no pixels above the sea-floor cut line ({:.2f} m): the Route subdivision produced no terrain in this chunk. Check the archetype's Route shape or raise Island.json's elevationMeters.", rLeafDir.string(), iRegionStartX, iRegionEndX - 1, iRegionStartY, iRegionEndY - 1, fCropCutLineMeters));
	}

	// Expand bbox symmetrically to a multiple of kiCropAlignment per axis, clamped to the region.
	// When clamping at a region edge consumes one side's padding, push the remainder onto the
	// opposite side so the final dimension still meets the alignment. The region span is itself a
	// multiple of kiCropAlignment (iTexturePixels is, and columns/rows divide it evenly), so the
	// worst case fits without overflowing the region.
	auto ExpandSpan = [](int64_t iLo, int64_t iHi, int64_t iClampLo, int64_t iClampHi, int64_t& riStart, int64_t& riSize)
	{
		int64_t iSpan = iHi - iLo + 1;
		int64_t iRequired = ((iSpan + kiCropAlignment - 1) / kiCropAlignment) * kiCropAlignment;
		int64_t iPad = iRequired - iSpan;
		int64_t iPadLow = iPad / 2;
		int64_t iPadHigh = iPad - iPadLow;
		int64_t iStart = iLo - iPadLow;
		int64_t iEnd = iHi + iPadHigh;
		if (iStart < iClampLo)
		{
			iEnd += iClampLo - iStart;
			iStart = iClampLo;
		}
		if (iEnd > iClampHi)
		{
			iStart -= iEnd - iClampHi;
			iEnd = iClampHi;
		}
		riStart = iStart;
		riSize = iEnd - iStart + 1;
	};

	int64_t iCropX = 0;
	int64_t iCropY = 0;
	int64_t iCropWidth = 0;
	int64_t iCropHeight = 0;
	ExpandSpan(iMinX, iMaxX, iRegionStartX, iRegionEndX - 1, iCropX, iCropWidth);
	ExpandSpan(iMinY, iMaxY, iRegionStartY, iRegionEndY - 1, iCropY, iCropHeight);
	LOG(kDefault, kDebug, "Cropping island chunk \"{}\": bbox ({}..{},{}..{}) -> ({}+{},{}+{}) [aligned to {}]", rLeafDir.string(), iMinX, iMaxX, iMinY, iMaxY, iCropX, iCropWidth, iCropY, iCropHeight, kiCropAlignment);

	// Crop elevation to iCropWidth × iCropHeight, then box-filter downsample by kiElevationDivisor
	// and write the leaf Elevation.r32. Downsampled dims are multiples of 4 because crop dims are
	// multiples of 4 × kiElevationDivisor.
	int64_t iElevationWidth = iCropWidth / kiElevationDivisor;
	int64_t iElevationHeight = iCropHeight / kiElevationDivisor;
	float fOneOverBoxSize = 1.0f / static_cast<float>(kiElevationDivisor * kiElevationDivisor);
	std::vector<float> downsampledPixels(static_cast<size_t>(iElevationWidth) * static_cast<size_t>(iElevationHeight));
	for (int64_t iOutY = 0; iOutY < iElevationHeight; ++iOutY)
	{
		for (int64_t iOutX = 0; iOutX < iElevationWidth; ++iOutX)
		{
			float fSum = 0.0f;
			for (int64_t iDy = 0; iDy < kiElevationDivisor; ++iDy)
			{
				const float* pfRow = &rFullElevationMeters.at(static_cast<size_t>(iCropY + iOutY * kiElevationDivisor + iDy) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX + iOutX * kiElevationDivisor));
				for (int64_t iDx = 0; iDx < kiElevationDivisor; ++iDx)
				{
					fSum += pfRow[iDx];
				}
			}
			downsampledPixels.at(static_cast<size_t>(iOutY) * static_cast<size_t>(iElevationWidth) + static_cast<size_t>(iOutX)) = fSum * fOneOverBoxSize;
		}
	}
	{
		std::ofstream writeStream(leafIntermediatesDir / "Elevation.r32", std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(downsampledPixels.data()), downsampledPixels.size() * sizeof(float));
	}

	// Crop AmbientOcclusion to the same bbox and write the leaf AmbientOcclusion.r16. Color.png and
	// Normals.exr stay full-res in the route Intermediates (no writer in Texture.cpp for either
	// format); ExportIsland crops their pixel data in-memory via this leaf's crop rect.
	{
		std::vector<uint16_t> aoCropped(static_cast<size_t>(iCropWidth) * static_cast<size_t>(iCropHeight));
		for (int64_t iY = 0; iY < iCropHeight; ++iY)
		{
			const uint16_t* puiSrc = &rFullAmbientOcclusion.at(static_cast<size_t>(iCropY + iY) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX));
			uint16_t* puiDst = &aoCropped.at(static_cast<size_t>(iY) * static_cast<size_t>(iCropWidth));
			std::memcpy(puiDst, puiSrc, static_cast<size_t>(iCropWidth) * sizeof(uint16_t));
		}
		std::ofstream writeStream(leafIntermediatesDir / "AmbientOcclusion.r16", std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(aoCropped.data()), aoCropped.size() * sizeof(uint16_t));
	}

	// Crop the (already beach-subdivided) mesh to this chunk's bbox and re-center its local origin
	// on the post-crop center. iCropX/iCropWidth map to engine X (Gaea X-east, no flip);
	// iCropY/iCropHeight map to engine Y with a sign flip (engine Y north-positive, heightmap row 0
	// = north edge). Discard triangles fully outside the bbox; keep partial-cross triangles for
	// silhouette quality (for a 2x1 split, a triangle straddling the seam is kept by both halves —
	// the same partial-cross behavior the single-island crop already uses at every edge). After the
	// index buffer is compacted, repack the vertex buffer to drop orphans, then re-center.
	int64_t iDiscardedTriangles = 0;
	{
		const double dPixelsToMeters = static_cast<double>(rDimensions.fFootprintMeters) / static_cast<double>(iTexturePixels);
		const float fCropMinXMeters = static_cast<float>(static_cast<double>(iCropX)                          * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
		const float fCropMaxXMeters = static_cast<float>(static_cast<double>(iCropX + iCropWidth)             * dPixelsToMeters - 0.5 * rDimensions.fFootprintMeters);
		const float fCropMaxYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(iCropY)               * dPixelsToMeters);
		const float fCropMinYMeters = static_cast<float>(0.5 * rDimensions.fFootprintMeters - static_cast<double>(iCropY + iCropHeight) * dPixelsToMeters);
		const float fCropCenterXMeters = 0.5f * (fCropMinXMeters + fCropMaxXMeters);
		const float fCropCenterYMeters = 0.5f * (fCropMinYMeters + fCropMaxYMeters);

		auto VertexOutside = [&meshPositions, fCropMinXMeters, fCropMaxXMeters, fCropMinYMeters, fCropMaxYMeters](uint32_t iV) -> bool
		{
			float fX = meshPositions[static_cast<size_t>(iV) * 3 + 0];
			float fY = meshPositions[static_cast<size_t>(iV) * 3 + 1];
			return fX < fCropMinXMeters || fX > fCropMaxXMeters || fY < fCropMinYMeters || fY > fCropMaxYMeters;
		};

		std::vector<uint32_t> survivingIndices;
		survivingIndices.reserve(meshIndices.size());
		for (size_t i = 0; i + 2 < meshIndices.size(); i += 3)
		{
			uint32_t iA = meshIndices[i + 0];
			uint32_t iB = meshIndices[i + 1];
			uint32_t iC = meshIndices[i + 2];
			if (VertexOutside(iA) && VertexOutside(iB) && VertexOutside(iC))
			{
				++iDiscardedTriangles;
				continue;
			}
			survivingIndices.push_back(iA);
			survivingIndices.push_back(iB);
			survivingIndices.push_back(iC);
		}
		meshIndices = std::move(survivingIndices);

		// Repack vertex buffer: walk indices to mark used vertices, then compact and remap.
		const int64_t iOldVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
		std::vector<uint32_t> oldToNew(static_cast<size_t>(iOldVertexCount), UINT32_MAX);
		std::vector<float> packedPositions;
		packedPositions.reserve(meshPositions.size());
		for (uint32_t& riIndex : meshIndices)
		{
			uint32_t& riRemap = oldToNew[riIndex];
			if (riRemap == UINT32_MAX)
			{
				riRemap = static_cast<uint32_t>(packedPositions.size() / 3);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 0]);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 1]);
				packedPositions.push_back(meshPositions[static_cast<size_t>(riIndex) * 3 + 2]);
			}
			riIndex = riRemap;
		}
		meshPositions = std::move(packedPositions);

		// Re-center XY of every surviving vertex on the post-crop center. Z is unchanged
		// (Z=0 is sea level globally, independent of horizontal crop).
		for (size_t iV = 0; iV < meshPositions.size() / 3; ++iV)
		{
			meshPositions[iV * 3 + 0] -= fCropCenterXMeters;
			meshPositions[iV * 3 + 1] -= fCropCenterYMeters;
		}
		LOG(kDefault, kDebug, "Mesh chunk \"{}\": cropped {} triangles outside bbox, re-centered XY by ({:.2f}, {:.2f})m, {} -> {} vertices", rLeafDir.string(), iDiscardedTriangles, fCropCenterXMeters, fCropCenterYMeters, iOldVertexCount, static_cast<int64_t>(meshPositions.size() / 3));
	}

	{
		std::ofstream meshOut(leafIntermediatesDir / "MeshProcessed.bin", std::ios::binary | std::ios::trunc);
		int32_t iVertexCount32 = static_cast<int32_t>(meshPositions.size() / 3);
		int32_t iIndexCount32 = static_cast<int32_t>(meshIndices.size());
		meshOut.write(reinterpret_cast<const char*>(&iVertexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(&iIndexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(meshPositions.data()), static_cast<std::streamsize>(meshPositions.size() * sizeof(float)));
		meshOut.write(reinterpret_cast<const char*>(meshIndices.data()), static_cast<std::streamsize>(meshIndices.size() * sizeof(uint32_t)));
	}

	// BakedDimensions.json, written LAST in the leaf — its presence is the leaf-complete marker
	// ExportIsland::Handles keys on. Anisotropic post-crop world dims (meters-per-pixel is global,
	// so the formula is unchanged from the single-island case), the crop rect into the full bake,
	// and the leaf-relative path to the shared texture sources.
	{
		nlohmann::json bakedJson;
		bakedJson["widthMeters"] = rDimensions.fFootprintMeters * static_cast<float>(iCropWidth) / static_cast<float>(iTexturePixels);
		bakedJson["heightMeters"] = rDimensions.fFootprintMeters * static_cast<float>(iCropHeight) / static_cast<float>(iTexturePixels);
		bakedJson["elevationMeters"] = rDimensions.fElevationMeters;
		bakedJson["cropX"] = iCropX;
		bakedJson["cropY"] = iCropY;
		bakedJson["cropWidth"] = iCropWidth;
		bakedJson["cropHeight"] = iCropHeight;
		bakedJson["fullTexturePixels"] = iTexturePixels;
		bakedJson["textureSourceDir"] = rTextureSourceDirRelative;
		std::ofstream bakedStream(leafIntermediatesDir / kpcBakedDimensionsFile);
		bakedStream << bakedJson.dump(4);
	}
}

// Bakes one route of one island in two stages. STAGE 1 (Gaea raw, slow — only when IsGaeaRawDirty):
// patch the route's Route Choice into a per-route archetype copy and run Gaea once at full
// texturePixels into the route's Intermediates/. STAGE 2 (split, fast — when IsGaeaRawDirty OR
// AreLeavesDirty): split the raw bake into iColumns × iRows chunk leaves via ProcessBakedRegion
// (1 leaf for 1x1, 2 for 2x1, 4 for 2x2). A split-only change re-runs Stage 2 against the existing Stage-1
// output — no Gaea re-export. Returns early when both stages are clean.
void BakeRoute(const std::filesystem::path& rGaeaExecutable, const std::filesystem::path& rIslandFolder, const std::filesystem::path& rArchetypeFile, const std::filesystem::path& rIslandJsonFile, const nlohmann::json& rIslandJson, const WorldDimensions& rDimensions, int32_t iSeed, int64_t iTexturePixels, std::optional<int64_t> oiMeshResolution, const RouteSubdivision& rRoute)
{
	std::filesystem::path routeDir = rIslandFolder / rRoute.pcLabel;
	std::filesystem::path intermediatesDir = routeDir / kpcIslandIntermediatesDir;
	int64_t iLeafCount = rRoute.iColumns * rRoute.iRows;

	bool bGaeaDirty = IsGaeaRawDirty(intermediatesDir, rIslandJsonFile, rArchetypeFile);
	bool bLeavesDirty = AreLeavesDirty(routeDir, iLeafCount);
	if (!bGaeaDirty && !bLeavesDirty)
	{
		return;
	}

	std::filesystem::create_directories(intermediatesDir);
	std::filesystem::path patchedArchetypeFile = intermediatesDir / kpcPatchedArchetypeFile;

	// STAGE 1 — Gaea raw export (slow). Skipped when the raw outputs are already present and fresh,
	// so a split-only change re-splits the existing bake without re-running Gaea.Swarm.
	if (bGaeaDirty)
	{
		LOG(kDefault, kDebug, "Baking island route \"{}\" (Gaea export; archetype: \"{}\", seed: {}, texturePixels: {}, Route Choice: {})", routeDir.string(), rArchetypeFile.string(), iSeed, iTexturePixels, rRoute.iGaeaChoice);

		// Strip DataPacker-owned keys; remaining keys become Gaea graph variables. routes /
		// widthMeters / elevationMeters / seed / texturePixels are DataPacker-consumed: Gaea's
		// --vars can't reach Terrain.{Width,Height}, the Route Choice, or per-node Seed fields.
		nlohmann::json varsJson = rIslandJson;
		for (const char* pcKey : kpcRequiredIslandJsonKeys)
		{
			varsJson.erase(pcKey);
		}
		varsJson.erase("meshResolution");  // DataPacker-consumed; patched into the Mesher node directly.

		// Gaea.Swarm.exe trips on `--vars` pointing to an empty JSON object ("{}") with an opaque
		// "System.IO.IOException: The handle is invalid" during variable load. Only emit the vars
		// file and pass --vars when there are user variables left. Per-route temp name so concurrent
		// routes / islands don't collide.
		bool bHasVars = !varsJson.empty();
		std::filesystem::path varsFile = gpFileManager->mTempDirectory / std::format("{}-{}.gaea-vars.json", rIslandFolder.filename().string(), rRoute.pcLabel);
		common::ScopedLambda varsFileCleanup([&varsFile, bHasVars]()
		{
			if (bHasVars)
			{
				std::filesystem::remove(varsFile);
			}
		});
		if (bHasVars)
		{
			std::ofstream varsStream(varsFile);
			varsStream << varsJson.dump();
			varsStream.close();
		}

		// Copy the source archetype into this route's Intermediates/ and patch the copy — the on-disk
		// source .terrain is never mutated. PatchArchetype sets this route's Route Choice along with
		// dims / seeds / Mesher resolution. PatchedArchetype.terrain doubles as a debug artifact (open
		// in Gaea to inspect exactly what was baked, including the patched Route Choice).
		std::filesystem::copy_file(rArchetypeFile, patchedArchetypeFile, std::filesystem::copy_options::overwrite_existing);
		PatchArchetype(patchedArchetypeFile, rDimensions, iSeed, oiMeshResolution, rRoute.iGaeaChoice);

		// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the new-console
		// helper. argv[0] is the executable's own path. --seed is dropped (per-node seeds were patched
		// into the archetype). Gaea bakes the patched copy in this route's Intermediates/.
		std::wstring commandLine;
		commandLine += L"\"" + rGaeaExecutable.native() + L"\"";
		commandLine += L" --silent";
		commandLine += L" --Filename \"" + patchedArchetypeFile.native() + L"\"";
		commandLine += L" --buildpath \"" + intermediatesDir.native() + L"\"";
		commandLine += std::format(L" --resolution {}", iTexturePixels);
		if (bHasVars)
		{
			commandLine += L" --vars \"" + varsFile.native() + L"\"";
		}

		LOG(kDefault, kDebug, "Running: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {}{}{}", rGaeaExecutable.string(), patchedArchetypeFile.string(), intermediatesDir.string(), iTexturePixels, bHasVars ? " --vars " : "", bHasVars ? varsFile.string() : "");

		common::ExecutableResult result = common::RunExecutableInNewConsole(rGaeaExecutable, commandLine);

		if (result.miExitCode != 0)
		{
			throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\". Use /gaea2-diagnose to examine the log file for failures.", result.miExitCode, routeDir.string()));
		}

		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (!std::filesystem::exists(intermediatesDir / pcFile))
			{
				throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses FloatRaw32, AmbientOcclusion uses UshortRaw16, Color uses PNG8, and Normals uses Exr.", routeDir.string(), pcFile, std::filesystem::path(pcFile).stem().string()));
			}
		}

		// Stamp the Gaea-raw sentinel now the raw outputs are verified, so a later split-only re-run
		// (stale leaves / kiSplitVersion bump) reuses them without re-running Gaea.
		std::ofstream gaeaVersionStream(intermediatesDir / kpcBakeVersionFile);
		gaeaVersionStream << kiBakeVersion;
	}
	else
	{
		LOG(kDefault, kDebug, "Reusing Gaea bake for route \"{}\"; re-splitting only (no Gaea export)", routeDir.string());
	}

	// STAGE 2 — split (fast). Raw outputs are present here (just baked, or reused fresh).
	// PatchArchetype doesn't touch the Sea node, so the patched copy keeps the authored Level (or
	// kfGaeaSeaLevelDefault); the per-island beach offset drives the elevation transform, auto-crop
	// cut line, and mesh-Z scrub.
	float fSeaLevelNormalized = ReadArchetypeSeaLevel(patchedArchetypeFile);
	float fBeachOffsetMeters = fSeaLevelNormalized * rDimensions.fElevationMeters;
	LOG(kDefault, kDebug, "Archetype Sea Level (read, not patched): {} → beach offset {} m for elevationMeters {} m", common::Wb(fSeaLevelNormalized, 4), common::Wb(fBeachOffsetMeters, 2), common::Wb(rDimensions.fElevationMeters, 2));

	// Per-island sea floor must match the engine-wide kfSeaBottomMeters so the elevation RTT clear
	// (Engine/Source/Graphics/Managers/RenderTargetTextures.cpp) blends seamlessly with edge texels.
	ASSERT(std::abs(-fBeachOffsetMeters - common::kfSeaBottomMeters) < 0.01f);

	// Read raw elevation and convert to engine-meters: pixel_m = (pixel_normalized -
	// fSeaLevelNormalized) × elevationMeters. Beach = 0, ocean negative (to -fBeachOffsetMeters),
	// land positive. NaN/Inf scrubbed to the sea floor (R32_SFLOAT is unbounded; a stray non-finite
	// pixel would poison the elevation G-buffer and vertex displacement). The full-res buffer feeds
	// every chunk's ProcessBakedRegion crop; the raw Elevation.r32 stays on disk as the bake source.
	std::filesystem::path elevationFile = intermediatesDir / "Elevation.r32";
	std::vector<float> fullElevationMeters(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
	int64_t iExpectedBytes = static_cast<int64_t>(fullElevationMeters.size() * sizeof(float));
	int64_t iActualBytes = static_cast<int64_t>(std::filesystem::file_size(elevationFile));
	if (iActualBytes != iExpectedBytes)
	{
		throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} float32). Verify the archetype's Elevation Export node uses FloatRaw32 format and is unconstrained by an internal resolution override.", elevationFile.string(), iActualBytes, iExpectedBytes, iTexturePixels, iTexturePixels));
	}
	{
		std::ifstream readStream(elevationFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(fullElevationMeters.data()), iExpectedBytes);
	}
	for (float& rfPixel : fullElevationMeters)
	{
		rfPixel = std::isfinite(rfPixel) ? (rfPixel - fSeaLevelNormalized) * rDimensions.fElevationMeters : -fBeachOffsetMeters;
	}

	// Read raw full-resolution AmbientOcclusion (cropped per chunk in ProcessBakedRegion).
	std::filesystem::path ambientOcclusionFile = intermediatesDir / "AmbientOcclusion.r16";
	std::vector<uint16_t> fullAmbientOcclusion(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
	{
		int64_t iExpectedAoBytes = iTexturePixels * iTexturePixels * static_cast<int64_t>(sizeof(uint16_t));
		int64_t iActualAoBytes = static_cast<int64_t>(std::filesystem::file_size(ambientOcclusionFile));
		if (iActualAoBytes != iExpectedAoBytes)
		{
			throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} uint16). Verify the archetype's AmbientOcclusion Export node uses UshortRaw16 format.", ambientOcclusionFile.string(), iActualAoBytes, iExpectedAoBytes, iTexturePixels, iTexturePixels));
		}
		std::ifstream readStream(ambientOcclusionFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(fullAmbientOcclusion.data()), iExpectedAoBytes);
	}

	// Parse Mesh.gltf (Mesher's glTF separate-format manifest; references Mesh.bin) into a flat
	// float3-positions / uint32-indices mesh in island-local meters, then run the adaptive
	// beach-band subdivision ONCE on the full mesh (the per-chunk region crops reuse the densified
	// result). glTF 2.0 mandates right-handed Y-up: Mesher emits (X_east, Y_height, Z_south); the
	// mapping (X,Y,Z)=(gltf.x, -gltf.z, gltf.y) has determinant +1 so handedness / CCW winding are
	// preserved, engine Y increases northward, engine Z is up. Height gets the per-island beach
	// offset subtracted so sea level = 0. UVs (TEXCOORD_0) are discarded — runtime derives
	// visible-area UV from world XY. Indices are upcast to uint32. The actual chunk crop / re-center
	// / write happens later in ProcessBakedRegion.
	std::vector<float> meshPositions;
	std::vector<uint32_t> meshIndices;
	{
		std::filesystem::path meshGltfFile = intermediatesDir / "Mesh.gltf";
		tinygltf::Model gltfModel;
		std::string error;
		std::string warning;
		tinygltf::TinyGLTF gltfContext;
		bool bLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, meshGltfFile.string());
		if (!bLoaded)
		{
			throw std::runtime_error(std::format("Failed to parse Gaea Mesher output \"{}\": {} (warning: {}). Verify the archetype has a Mesher node with Format=GLTF and that Gaea wrote both Mesh.gltf and Mesh.bin.", meshGltfFile.string(), error, warning));
		}
		if (gltfModel.meshes.empty() || gltfModel.meshes[0].primitives.empty())
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" contains no mesh primitives.", meshGltfFile.string()));
		}

		const tinygltf::Primitive& rPrimitive = gltfModel.meshes[0].primitives[0];
		auto positionIt = rPrimitive.attributes.find("POSITION");
		if (positionIt == rPrimitive.attributes.end() || rPrimitive.indices < 0)
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" primitive missing POSITION attribute or indices accessor.", meshGltfFile.string()));
		}

		const tinygltf::Accessor& rPosAccessor = gltfModel.accessors.at(static_cast<size_t>(positionIt->second));
		const tinygltf::BufferView& rPosView = gltfModel.bufferViews.at(static_cast<size_t>(rPosAccessor.bufferView));
		const tinygltf::Buffer& rPosBuffer = gltfModel.buffers.at(static_cast<size_t>(rPosView.buffer));
		if (rPosAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || rPosAccessor.type != TINYGLTF_TYPE_VEC3)
		{
			throw std::runtime_error(std::format("Gaea Mesher output \"{}\" POSITION accessor is not float3.", meshGltfFile.string()));
		}

		int64_t iVertexCount = static_cast<int64_t>(rPosAccessor.count);
		meshPositions.resize(static_cast<size_t>(iVertexCount) * 3);
		{
			const std::byte* pSrc = reinterpret_cast<const std::byte*>(rPosBuffer.data.data()) + rPosView.byteOffset + rPosAccessor.byteOffset;
			size_t uiStride = rPosAccessor.ByteStride(rPosView);
			for (int64_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
			{
				const float* pfXyz = reinterpret_cast<const float*>(pSrc + static_cast<size_t>(iVertex) * uiStride);
				float fX = pfXyz[0];
				float fY = -pfXyz[2];
				float fZ = std::isfinite(pfXyz[1]) ? pfXyz[1] - fBeachOffsetMeters : -fBeachOffsetMeters;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 0] = fX;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 1] = fY;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 2] = fZ;
			}
		}

		const tinygltf::Accessor& rIdxAccessor = gltfModel.accessors.at(static_cast<size_t>(rPrimitive.indices));
		const tinygltf::BufferView& rIdxView = gltfModel.bufferViews.at(static_cast<size_t>(rIdxAccessor.bufferView));
		const tinygltf::Buffer& rIdxBuffer = gltfModel.buffers.at(static_cast<size_t>(rIdxView.buffer));
		int64_t iIndexCount = static_cast<int64_t>(rIdxAccessor.count);
		meshIndices.resize(static_cast<size_t>(iIndexCount));
		{
			const std::byte* pSrc = reinterpret_cast<const std::byte*>(rIdxBuffer.data.data()) + rIdxView.byteOffset + rIdxAccessor.byteOffset;
			switch (rIdxAccessor.componentType)
			{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					std::memcpy(meshIndices.data(), pSrc, static_cast<size_t>(iIndexCount) * sizeof(uint32_t));
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				{
					const uint16_t* puiSrc = reinterpret_cast<const uint16_t*>(pSrc);
					for (int64_t i = 0; i < iIndexCount; ++i)
					{
						meshIndices[static_cast<size_t>(i)] = puiSrc[i];
					}
					break;
				}
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
				{
					const uint8_t* puiSrc = reinterpret_cast<const uint8_t*>(pSrc);
					for (int64_t i = 0; i < iIndexCount; ++i)
					{
						meshIndices[static_cast<size_t>(i)] = puiSrc[i];
					}
					break;
				}
				default:
					throw std::runtime_error(std::format("Gaea Mesher output \"{}\" indices accessor has unsupported componentType {}.", meshGltfFile.string(), rIdxAccessor.componentType));
			}
		}

		int64_t iInitialVertexCount = iVertexCount;
		int64_t iInitialTriangleCount = iIndexCount / 3;

		// Adaptive beach-band subdivision. Densifies triangles whose Z-range overlaps the band so
		// the shore silhouette and shore-material blend get enough vertex resolution. In-band tris
		// do 1->4 midpoint splits until longest XY edge <= kfBeachSubdivisionMaxEdgeMeters. Out-of-band
		// neighbors that inherit a midpoint via a shared edge do the MINIMAL absorption split
		// (1->2 for one midpoint, 1->3 for two, 1->4 for all three) -- introducing no new midpoints,
		// so the cascade dies at one ring. Linear-interp Z for new midpoints is fine because
		// Terrain.vert overrides Z from the heightmap at rasterization; mesh Z exists only for the
		// in-band test, and an in-band split's children inherit Z values that are subsets of the
		// parent's range. Runs once on the full mesh; chunk crops below reuse the densified mesh.
		float fBandMinZ = kfBeachSubdivisionMinMeters;
		float fBandMaxZ = kfBeachSubdivisionMaxMeters;
		int64_t iDepthCapHits = 0;
		SubdivideBeachBand(meshPositions, meshIndices, fBandMinZ, fBandMaxZ, kfBeachSubdivisionMaxEdgeMeters, kiBeachSubdivisionMaxDepth, iDepthCapHits);
		iVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
		iIndexCount = static_cast<int64_t>(meshIndices.size());

		if (iDepthCapHits > 0)
		{
			LOG(kDefault, kWarning, "Mesh \"{}\": beach subdivision hit depth cap ({}) on {} triangle(s); largest input triangles may still exceed {:.2f}m edge target AND the mesh may contain T-junction cracks where capped absorption-needing triangles were skipped (raise kiBeachSubdivisionMaxDepth or split it into separate in-band / absorption caps if observed)", routeDir.string(), kiBeachSubdivisionMaxDepth, iDepthCapHits, kfBeachSubdivisionMaxEdgeMeters);
		}
		LOG(kDefault, kDebug, "Mesh \"{}\": {} -> {} vertices, {} -> {} triangles after beach subdivision (band Z=[{:.2f}, {:.2f}]m, edge target {:.2f}m)", routeDir.string(), iInitialVertexCount, iVertexCount, iInitialTriangleCount, iIndexCount / 3, fBandMinZ, fBandMaxZ, kfBeachSubdivisionMaxEdgeMeters);
	}

	// Split into chunks (1 for 1x1, iColumns × iRows otherwise) and write each leaf. The X / Y
	// region boundaries divide the full texture evenly; ProcessBakedRegion auto-crops within each.
	// textureSourceDir is leaf-relative ("../Intermediates") so ExportIsland reads the shared
	// full-res Color / Normals / mask sources from this route's Intermediates dir.
	std::string textureSourceDirRelative = std::format("../{}", kpcIslandIntermediatesDir);
	for (int64_t iColumn = 0; iColumn < rRoute.iColumns; ++iColumn)
	{
		for (int64_t iRow = 0; iRow < rRoute.iRows; ++iRow)
		{
			int64_t iChunkIndex = iColumn * rRoute.iRows + iRow;
			int64_t iRegionStartX = iColumn * iTexturePixels / rRoute.iColumns;
			int64_t iRegionEndX = (iColumn + 1) * iTexturePixels / rRoute.iColumns;
			int64_t iRegionStartY = iRow * iTexturePixels / rRoute.iRows;
			int64_t iRegionEndY = (iRow + 1) * iTexturePixels / rRoute.iRows;
			std::filesystem::path leafDir = routeDir / std::to_string(iChunkIndex);
			ProcessBakedRegion(fullElevationMeters, fullAmbientOcclusion, meshPositions, meshIndices, iTexturePixels, rDimensions, iRegionStartX, iRegionEndX, iRegionStartY, iRegionEndY, fBeachOffsetMeters, leafDir, textureSourceDirRelative);
		}
	}

	// Stamp the split sentinel last, after every leaf is written. A crash mid-split leaves it absent
	// (or stale), so AreLeavesDirty re-splits next run — without re-running Gaea (BakeVersion is
	// already stamped above, so IsGaeaRawDirty stays clean).
	{
		std::ofstream splitVersionStream(intermediatesDir / kpcSplitVersionFile);
		splitVersionStream << kiSplitVersion;
	}

	LOG(kDefault, kDebug, "Island route \"{}\" ready ({} chunk(s){})", routeDir.string(), iLeafCount, bGaeaDirty ? ", Gaea re-baked" : ", split-only reuse");
}

void BakeOne(const std::filesystem::path& rGaeaExecutable, const std::filesystem::path& rIslandFolder)
{
	std::filesystem::path islandJsonFile = rIslandFolder / "Island.json";

	std::ifstream islandStream(islandJsonFile);
	nlohmann::json islandJson = nlohmann::json::parse(islandStream);
	islandStream.close();

	if (!islandJson.is_object())
	{
		throw std::runtime_error(std::format("\"{}\" must be a JSON object", islandJsonFile.string()));
	}

	// All schema fields are strictly required. Catches stale configs (e.g., legacy "mips" key) and
	// avoids silent implicit-zero seeds or unscaled archetype dimensions.
	for (const char* pcRequired : kpcRequiredIslandJsonKeys)
	{
		if (!islandJson.contains(pcRequired))
		{
			throw std::runtime_error(std::format("\"{}\" is missing required key \"{}\". Schema is: archetype (string), seed (int), widthMeters (float), elevationMeters (float), texturePixels (int), routes (array of label strings, e.g. [\"1x1\", \"2x1\"]).", islandJsonFile.string(), pcRequired));
		}
	}

	if (islandJson.contains("mips"))
	{
		throw std::runtime_error(std::format("\"{}\" has legacy \"mips\" key. Islands no longer use mip chains: remove \"mips\" and use \"texturePixels\" instead (single resolution; elevation auto-downsamples to texturePixels / {}).", islandJsonFile.string(), kiElevationDivisor));
	}

	int32_t iSeed = islandJson.at("seed").get<int32_t>();
	int64_t iTexturePixels = islandJson.at("texturePixels").get<int64_t>();
	WorldDimensions dimensions
	{
		.fFootprintMeters = islandJson.at("widthMeters").get<float>(),
		.fElevationMeters = islandJson.at("elevationMeters").get<float>(),
	};

	// Optional: override the Mesher's VerticesPerSide (per-island mesh density). When absent, leave
	// whatever value the archetype's Mesher node already has. Stripped from varsJson so it doesn't
	// reach Gaea as a graph variable.
	std::optional<int64_t> oiMeshResolution;
	if (islandJson.contains("meshResolution"))
	{
		oiMeshResolution = islandJson.at("meshResolution").get<int64_t>();
	}

	// texturePixels must be >= and a multiple of kiCropAlignment so the per-axis auto-crop dims
	// satisfy both BC block alignment and Vulkan transfer-queue granularity (block-relative for
	// compressed formats) on every BC + elevation upload. See kiCropAlignment's definition.
	if (iTexturePixels < kiCropAlignment || (iTexturePixels % kiCropAlignment) != 0)
	{
		throw std::runtime_error(std::format("\"{}\" texturePixels {} is invalid: must be >= {} and a multiple of {} so the auto-crop dims stay BC-block- and Vulkan-transfer-granularity-aligned.", islandJsonFile.string(), iTexturePixels, kiCropAlignment, kiCropAlignment));
	}

	// Resolve the routes list (one or many labels). Each must exist in kRouteSubdivisions; a split
	// route additionally requires every per-column / per-row pixel span stay >= the crop alignment.
	const nlohmann::json& rRoutesJson = islandJson.at("routes");
	if (!rRoutesJson.is_array() || rRoutesJson.empty())
	{
		throw std::runtime_error(std::format("\"{}\" \"routes\" must be a non-empty array of label strings (e.g. [\"1x1\", \"2x1\"]).", islandJsonFile.string()));
	}
	std::vector<const RouteSubdivision*> routes;
	for (const nlohmann::json& rRouteJson : rRoutesJson)
	{
		if (!rRouteJson.is_string())
		{
			throw std::runtime_error(std::format("\"{}\" \"routes\" entries must be label strings (e.g. \"1x1\").", islandJsonFile.string()));
		}
		const RouteSubdivision& rRoute = LookupRouteSubdivision(rRouteJson.get<std::string>(), islandJsonFile);
		// Each split axis must divide texturePixels into chunks whose per-chunk pixel span is itself a
		// multiple of kiCropAlignment, so ProcessBakedRegion's per-region ExpandSpan never overflows
		// the region and every BC crop stays block-aligned. `texturePixels % (divisions * alignment) == 0`
		// is exactly "texturePixels / divisions is an integer multiple of alignment". (1x1 passes since
		// texturePixels is already a multiple of alignment.)
		if ((iTexturePixels % (rRoute.iColumns * kiCropAlignment)) != 0
			|| (iTexturePixels % (rRoute.iRows * kiCropAlignment)) != 0)
		{
			throw std::runtime_error(std::format("\"{}\" route \"{}\" splits texturePixels {} into {}x{} chunks, but a per-chunk pixel span would not be a multiple of the {}-pixel crop alignment. Raise texturePixels or choose a subdivision that divides it into {}-aligned chunks.", islandJsonFile.string(), rRoute.pcLabel, iTexturePixels, rRoute.iColumns, rRoute.iRows, kiCropAlignment, kiCropAlignment));
		}
		routes.push_back(&rRoute);
	}

	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);

	// Prune stale sub-folders: any directory whose name isn't a current route label. Migrates the
	// pre-routes top-level Intermediates/ layout away and drops folders for routes removed from the
	// list, so no orphaned leaf produces a stale chunk. Intermediates are build cache, not source;
	// Island.json and the archetype .terrain are files, so this never touches them.
	// Collect stale sub-folders first, then delete — mutating the directory mid-iteration via
	// remove_all is unspecified behavior for directory_iterator.
	std::vector<std::filesystem::path> staleSubFolders;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rIslandFolder))
	{
		if (!rEntry.is_directory())
		{
			continue;
		}
		std::string name = rEntry.path().filename().string();
		bool bIsRouteLabel = false;
		for (const RouteSubdivision* pRoute : routes)
		{
			if (name == pRoute->pcLabel)
			{
				bIsRouteLabel = true;
				break;
			}
		}
		if (!bIsRouteLabel)
		{
			staleSubFolders.push_back(rEntry.path());
		}
	}
	for (const std::filesystem::path& rStaleSubFolder : staleSubFolders)
	{
		std::filesystem::remove_all(rStaleSubFolder);
		LOG(kDefault, kDebug, "Removed stale island sub-folder: \"{}\"", rStaleSubFolder.string());
	}

	for (const RouteSubdivision* pRoute : routes)
	{
		BakeRoute(rGaeaExecutable, rIslandFolder, archetypeFile, islandJsonFile, islandJson, dimensions, iSeed, iTexturePixels, oiMeshResolution, *pRoute);
	}
}

} // namespace

WorldDimensions GetIslandDimensions(const std::filesystem::path& rIslandFolder)
{
	std::filesystem::path islandJsonFile = rIslandFolder / "Island.json";
	std::ifstream islandStream(islandJsonFile);
	nlohmann::json islandJson = nlohmann::json::parse(islandStream);
	islandStream.close();

	return WorldDimensions
	{
		.fFootprintMeters = islandJson.at("widthMeters").get<float>(),
		.fElevationMeters = islandJson.at("elevationMeters").get<float>(),
	};
}

BakedDimensions ReadBakedDimensions(const std::filesystem::path& rLeafFolder)
{
	std::filesystem::path bakedJsonFile = rLeafFolder / kpcIslandIntermediatesDir / kpcBakedDimensionsFile;
	std::ifstream bakedStream(bakedJsonFile);
	nlohmann::json bakedJson = nlohmann::json::parse(bakedStream);
	bakedStream.close();

	return BakedDimensions
	{
		.fWidthMeters = bakedJson.at("widthMeters").get<float>(),
		.fHeightMeters = bakedJson.at("heightMeters").get<float>(),
		.fElevationMeters = bakedJson.at("elevationMeters").get<float>(),
		.iCropX = bakedJson.at("cropX").get<int64_t>(),
		.iCropY = bakedJson.at("cropY").get<int64_t>(),
		.iCropWidth = bakedJson.at("cropWidth").get<int64_t>(),
		.iCropHeight = bakedJson.at("cropHeight").get<int64_t>(),
		.iFullTexturePixels = bakedJson.at("fullTexturePixels").get<int64_t>(),
		.textureSourceDir = bakedJson.at("textureSourceDir").get<std::string>(),
	};
}

void BakeIslandIntermediates()
{
	std::vector<std::filesystem::path> islandFolders;
	for (const std::filesystem::path& rInputDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path islandsRoot = rInputDirectory / "Islands";
		if (!std::filesystem::exists(islandsRoot))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(islandsRoot))
		{
			if (rEntry.is_directory() && std::filesystem::exists(rEntry.path() / "Island.json"))
			{
				islandFolders.push_back(rEntry.path());
			}
		}
	}

	if (islandFolders.empty())
	{
		return;
	}

	std::sort(islandFolders.begin(), islandFolders.end());

	std::filesystem::path gaeaExecutable = ResolveGaeaExecutable();

	for (const std::filesystem::path& rIslandFolder : islandFolders)
	{
		BakeOne(gaeaExecutable, rIslandFolder);
	}
}
