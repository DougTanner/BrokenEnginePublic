#include "BakeIslandIntermediates.h"

#include "FileManager.h"
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

// Single intermediates folder per island (folder name kpcIslandIntermediatesDir, see
// ExportIsland.h). One Gaea bake produces all four files at texturePixels resolution; the
// elevation is then downsampled in-process to texturePixels / kiElevationDivisor and rewritten
// in place. Elevation.r32 is headerless IEEE-754 float (Gaea's FloatRaw32 format), normalized
// [0,1] at bake time — DataPacker scales to meters by elevationMeters and subtracts the constant
// kfBeachHeightMeters so beach = 0 and the sea floor sits at -kfBeachHeightMeters. (The archetype's
// Sea node Level is patched to kfBeachHeightMeters / elevationMeters before each bake so Gaea
// actually emits the water surface at that altitude.) AmbientOcclusion.r16 stays
// UshortRaw16 (precision-matched to BC4_UNORM). Color/Normals stay multi-channel EXR.
// Files written by Gaea directly. Used for both the post-Gaea existence verification and the
// IsBakeDirty existence check.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.exr",
	"Elevation.r32",
	"Normals.exr",
	"Mesh.gltf",  // Gaea Mesher output: glTF JSON manifest (separate-format)
	"Mesh.bin",   // Gaea Mesher output: binary buffer referenced by Mesh.gltf
};

// Files DataPacker derives from the Gaea outputs above (positions + indices in island-local
// meters, XY-centered, sea-level Z=0). Tracked separately so the post-Gaea-return verification
// doesn't incorrectly demand them from Gaea itself; the bake-version sentinel and IsBakeDirty
// derived-file check cover invalidation when they go missing.
constexpr const char* kpcDerivedIntermediateFiles[] =
{
	"MeshProcessed.bin",
};

// Bake formula/algorithm version. Bump whenever BakeOne's elevation transform, downsample, or
// any other code-only behavior of the bake changes (i.e., anything not captured by Island.json
// or archetype mtimes). IsBakeDirty reads `Intermediates/BakeVersion.txt` and forces a re-bake
// if the recorded version doesn't match; the bake writes the current version on success.
constexpr int32_t kiBakeVersion = 22;

// Constant beach altitude (engine-meters). Sea surface and "beach line" are the same height for
// every island; sea floor sits at -kfBeachHeightMeters below the beach. Per-island elevationMeters
// only scales above-beach relief. The archetype Sea node's Level is patched to
// kfBeachHeightMeters / elevationMeters before each Gaea bake (then restored), so Gaea emits
// normalized [0,1] elevation with the water surface at that value; DataPacker subtracts
// kfBeachHeightMeters during the bake (in BakeOne, post-Gaea) so beach = 0 in engine space.
constexpr float kfBeachHeightMeters = 5.0f;

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
// floor is at -kfBeachHeightMeters (constant) so with kfBeachHeightMeters = 5 m, epsilon = 1.0 m
// means the cut line lives at -4 m, keeping the entire above-water landmass plus a halo of
// shallow water around the coastline.
constexpr float kfCropEpsilonAboveSeaFloorMeters = 1.0f;
constexpr const char* kpcBakeVersionFile = "BakeVersion.txt";

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

bool IsBakeDirty(const std::filesystem::path& rIslandFolder, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile)
{
	std::filesystem::path intermediatesDir = rIslandFolder / kpcIslandIntermediatesDir;
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(intermediatesDir / pcFile))
		{
			return true;
		}
	}
	for (const char* pcFile : kpcDerivedIntermediateFiles)
	{
		if (!std::filesystem::exists(intermediatesDir / pcFile))
		{
			return true;
		}
	}

	// Bake-version sentinel: catches code-only changes to the bake formula (e.g., the Gaea
	// pixel-inversion fix) that don't bump Island.json/archetype mtimes. Missing file or
	// version mismatch forces a re-bake.
	std::filesystem::path versionFile = intermediatesDir / kpcBakeVersionFile;
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
		if (std::filesystem::last_write_time(intermediatesDir / pcFile) < inputNewest)
		{
			return true;
		}
	}

	return false;
}

// Cross-process file lock around patch+bake+restore. Win32 named mutexes (Main.cpp:783) are
// per-machine kernel objects and cannot synchronize across separate Windows hosts hitting an
// archetype on a shared SMB drive. LockFileEx grants an advisory byte-range lock that the SMB
// server enforces across clients — the only Win32 mechanism that actually serializes cross-host
// writers to the same archetype .terrain file. The lock file is a persistent sibling
// "<archetype>.lock" matched by the *.terrain.lock rule in .gitignore so it never gets committed.
class ArchetypeLock
{
public:
	explicit ArchetypeLock(const std::filesystem::path& rArchetypeFile)
	{
		std::filesystem::path lockFile = std::filesystem::weakly_canonical(rArchetypeFile);
		lockFile += L".lock";

		mhLock = CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		__assume(mhLock != INVALID_HANDLE_VALUE);

		OVERLAPPED overlapped {};
		while (!LockFileEx(mhLock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &overlapped))
		{
			// Another process (typically a peer DataPacker on a different host via SMB) holds the lock.
			// Log once per interval to make the wait visible, then re-poll until acquired.
			LOG(kDefault, kInfo, "Waiting on cross-process archetype lock: \"{}\"", lockFile.string());
			Sleep(10000);
		}
	}

	~ArchetypeLock()
	{
		OVERLAPPED overlapped {};
		UnlockFileEx(mhLock, 0, MAXDWORD, MAXDWORD, &overlapped);
		CloseHandle(mhLock);
	}

	ArchetypeLock(const ArchetypeLock&) = delete;
	ArchetypeLock& operator=(const ArchetypeLock&) = delete;
	ArchetypeLock(ArchetypeLock&&) = delete;
	ArchetypeLock& operator=(ArchetypeLock&&) = delete;

private:
	HANDLE mhLock = INVALID_HANDLE_VALUE;
};

std::string ReadFileBytes(const std::filesystem::path& rFile)
{
	std::ifstream stream(rFile, std::ios::binary);
	std::stringstream buffer;
	buffer << stream.rdbuf();
	return buffer.str();
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

// Walks Terrain.Nodes for the Sea node and sets its Level to kfBeachHeightMeters / elevationMeters.
// Gaea's Level field is normalized [0,1]; with kfBeachHeightMeters = 5 m and elevationMeters = 100 m,
// this writes 0.05 so the baked terrain has its water surface at the desired absolute altitude.
// Throws if elevationMeters < kfBeachHeightMeters (Level would exceed 1.0, leaving Gaea behavior
// undefined). Nodes is a dict keyed by node ID, so we scan values for the one whose $type starts
// with the Gaea Sea node prefix. ShoreHeight is left as-is — it drives Gaea's shoreline visual
// effects (sand band, erosion) and is a separate calculation from the elevation pipeline.
void PatchArchetypeSeaLevel(nlohmann::json& rTerrainJson, float fElevationMeters)
{
	if (fElevationMeters < kfBeachHeightMeters)
	{
		throw std::runtime_error(std::format("Island.json elevationMeters ({:.2f}) is below the constant beach height ({:.2f} m). Raise elevationMeters so it is >= kfBeachHeightMeters, otherwise the Sea node Level patch would exceed Gaea's normalized [0,1] range.", fElevationMeters, kfBeachHeightMeters));
	}

	nlohmann::json& rNodes = rTerrainJson.at("Assets").at("$values").at(0).at("Terrain").at("Nodes");
	for (auto& [rKey, rNode] : rNodes.items())
	{
		if (!rNode.is_object() || !rNode.contains("$type"))
		{
			continue;
		}
		std::string type = rNode.at("$type").get<std::string>();
		if (type.starts_with("QuadSpinner.Gaea.Nodes.Sea"))
		{
			rNode.at("Level") = kfBeachHeightMeters / fElevationMeters;
			return;
		}
	}
	throw std::runtime_error("Archetype has no Sea node — DataPacker patches its Level to position the water surface at the constant kfBeachHeightMeters. Add a Sea node to the graph or extend BakeIslandIntermediates to handle sea-less archetypes.");
}

// Patches the archetype in place: Terrain.{Width,Height}, every per-node Seed, optional
// Mesher.VerticesPerSide, and the Sea node's Level (driven by kfBeachHeightMeters / elevationMeters
// so the baked water surface lands at a constant absolute altitude). The pre-patch bytes/mtime are
// restored on scope exit by the RAII guard in BakeOne, so this mutation is invisible to git.
void PatchArchetype(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions, int32_t iSeed, std::optional<int64_t> oiMeshResolution)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	rTerrain.at("Width") = rDimensions.fFootprintMeters;
	rTerrain.at("Height") = rDimensions.fElevationMeters;

	PatchArchetypeSeeds(terrainJson, iSeed);

	if (oiMeshResolution.has_value())
	{
		PatchArchetypeMesherResolution(terrainJson, *oiMeshResolution);
	}

	PatchArchetypeSeaLevel(terrainJson, rDimensions.fElevationMeters);

	WriteFileBytes(rTerrainFile, terrainJson.dump(2));
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

	// Single source of truth for required Island.json keys. Drives both the strict-presence check
	// and the varsJson strip pass below (which forwards remaining keys to Gaea as graph variables).
	// Adding a new DataPacker-owned key means appending here only.
	constexpr const char* kpcRequiredIslandJsonKeys[] = {"archetype", "seed", "widthMeters", "elevationMeters", "texturePixels"};

	// All schema fields are strictly required. Catches stale configs (e.g., legacy "mips" key)
	// and avoids silent implicit-zero seeds or unscaled archetype dimensions.
	for (const char* pcRequired : kpcRequiredIslandJsonKeys)
	{
		if (!islandJson.contains(pcRequired))
		{
			throw std::runtime_error(std::format("\"{}\" is missing required key \"{}\". Schema is: archetype (string), seed (int), widthMeters (float), elevationMeters (float), texturePixels (int).", islandJsonFile.string(), pcRequired));
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
	// whatever value the archetype's Mesher node already has. Stripped from varsJson below so it
	// doesn't reach Gaea as a graph variable.
	std::optional<int64_t> oiMeshResolution;
	if (islandJson.contains("meshResolution"))
	{
		oiMeshResolution = islandJson.at("meshResolution").get<int64_t>();
	}

	// Crop dimensions must satisfy two independent constraints, which converge on the same number:
	//
	//   BC color/normals/AO (4x4 blocks, runtime BC encoded):
	//     crop_pixels % kiBcBlockSize == 0                                    (BC block alignment)
	//     (crop_pixels / kiBcBlockSize) % kiTransferGranularityBlocks == 0    (Vulkan validator
	//                                                                          interprets queue family
	//                                                                          minImageTransferGranularity
	//                                                                          as BLOCK-relative for
	//                                                                          compressed formats)
	//     -> crop_pixels % (kiBcBlockSize * kiTransferGranularityBlocks) == 0
	//
	//   Elevation (R32_SFLOAT, downsampled by kiElevationDivisor):
	//     elev_pixels = crop_pixels / kiElevationDivisor
	//     elev_pixels % kiTransferGranularityBlocks == 0                      (uncompressed: granularity
	//                                                                          is in pixels directly)
	//     -> crop_pixels % (kiElevationDivisor * kiTransferGranularityBlocks) == 0
	//
	// max(...) picks the tighter of the two. Today blockSize == kiElevationDivisor == 4, so both
	// yield 64; the max() keeps the formula correct if either constant changes (e.g., bumping
	// kiElevationDivisor to 8 for further downsample tightens the elevation path to 128).
	// kiTransferGranularityBlocks = 16 covers every dedicated transfer queue we've seen in the
	// wild (NVIDIA + AMD discrete report 16 in pixels = 4 blocks; some integrated GPUs report
	// (1,1,1) and pass trivially under the same alignment).
	// iTexturePixels itself must satisfy the same multiple so the worst-case bbox-near-edge
	// expansion (padding pushed entirely to the opposite side) cannot overflow image bounds.
	constexpr int64_t kiBcBlockSize = 4;
	constexpr int64_t kiTransferGranularityBlocks = 16;
	int64_t iAlignmentRequirement = std::max(kiBcBlockSize, kiElevationDivisor) * kiTransferGranularityBlocks;
	if (iTexturePixels < iAlignmentRequirement || (iTexturePixels % iAlignmentRequirement) != 0)
	{
		throw std::runtime_error(std::format("\"{}\" texturePixels {} is invalid: must be >= {} and a multiple of {} so the auto-crop dims satisfy both BC block alignment and Vulkan transfer-queue granularity (block-relative for compressed formats) on every BC + elevation upload.", islandJsonFile.string(), iTexturePixels, iAlignmentRequirement, iAlignmentRequirement));
	}

	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);

	if (!IsBakeDirty(rIslandFolder, islandJsonFile, archetypeFile))
	{
		auto [date, time] = common::FileTimeString(std::filesystem::last_write_time(archetypeFile));
		LOG(kDefault, kDebug, "Skipping island bake (clean): \"{}\" (archetype \"{}\" mtime: {} {})", rIslandFolder.string(), archetypeFile.filename().string(), date, time);
		return;
	}

	LOG(kDefault, kDebug, "Baking island intermediates: \"{}\" (archetype: \"{}\", seed: {}, texturePixels: {})", rIslandFolder.string(), archetypeFile.string(), iSeed, iTexturePixels);

	// Strip DataPacker-owned keys; remaining keys become Gaea variables. widthMeters /
	// elevationMeters / seed drive the archetype patch directly (Gaea's --vars can't reach
	// Terrain.{Width,Height} or per-node Seed fields), so stripping them keeps the vars JSON to
	// true graph variables. texturePixels drives Gaea's --resolution flag; not a graph variable.
	nlohmann::json varsJson = islandJson;
	for (const char* pcKey : kpcRequiredIslandJsonKeys)
	{
		varsJson.erase(pcKey);
	}
	varsJson.erase("meshResolution");  // DataPacker-consumed; patched into the Mesher node directly.

	// Gaea.Swarm.exe trips on `--vars` pointing to an empty JSON object ("{}") with an opaque
	// "System.IO.IOException: The handle is invalid" during variable load. Only emit the vars
	// file and pass --vars when there are user variables left to forward.
	bool bHasVars = !varsJson.empty();
	std::filesystem::path varsFile = gpFileManager->mTempDirectory / (rIslandFolder.filename().string() + ".gaea-vars.json");
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

	// Prune legacy per-mip folders from earlier bake layouts (mip0/, mip1/, ...) so a fresh pull
	// onto an old tree doesn't leave stale intermediates around. Also handles the case where a
	// previous run was interrupted mid-bake.
	for (int64_t i = 0; ; ++i)
	{
		std::filesystem::path staleMipDir = rIslandFolder / std::format("mip{}", i);
		if (!std::filesystem::exists(staleMipDir))
		{
			break;
		}
		std::filesystem::remove_all(staleMipDir);
		LOG(kDefault, kDebug, "Removed stale mip directory: \"{}\"", staleMipDir.string());
	}

	// Patch the archetype's intrinsic Terrain.{Width,Height} AND every per-node "Seed" field
	// in-place around the bake, then restore byte-and-mtime-identical state on scope exit. The
	// cross-process file lock covers concurrent DataPacker instances on this host (Main.cpp:783
	// already serializes those, but defense-in-depth) and on other Windows hosts hitting the
	// same archetype over SMB. The ScopedLambda restore destroys before archetypeLock (LIFO),
	// so the lock is still held while the upstream bytes/mtime are written back, guaranteeing
	// a successful or failed bake leaves the archetype invisible to git and to IsBakeDirty's
	// mtime check.
	ArchetypeLock archetypeLock(archetypeFile);

	std::string originalArchetypeBytes = ReadFileBytes(archetypeFile);
	std::filesystem::file_time_type originalArchetypeModificationTime = std::filesystem::last_write_time(archetypeFile);
	common::ScopedLambda restoreArchetype([&archetypeFile, &originalArchetypeBytes, &originalArchetypeModificationTime]()
	{
		WriteFileBytes(archetypeFile, originalArchetypeBytes);
		std::filesystem::last_write_time(archetypeFile, originalArchetypeModificationTime);
	});

	PatchArchetype(archetypeFile, dimensions, iSeed, oiMeshResolution);

	std::filesystem::path intermediatesDir = rIslandFolder / kpcIslandIntermediatesDir;
	std::filesystem::create_directories(intermediatesDir);

	// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the
	// new-console helper rather than piped capture (the latter trips an IOException at
	// Gaea startup). argv[0] is the executable's own path so cmdline starts with the
	// quoted exe. --seed is dropped: per-node seeds were patched into the archetype above.
	std::wstring commandLine;
	commandLine += L"\"" + rGaeaExecutable.native() + L"\"";
	commandLine += L" --silent";
	commandLine += L" --Filename \"" + archetypeFile.native() + L"\"";
	commandLine += L" --buildpath \"" + intermediatesDir.native() + L"\"";
	commandLine += std::format(L" --resolution {}", iTexturePixels);
	if (bHasVars)
	{
		commandLine += L" --vars \"" + varsFile.native() + L"\"";
	}

	LOG(kDefault, kDebug, "Running: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {}{}{}", rGaeaExecutable.string(), archetypeFile.string(), intermediatesDir.string(), iTexturePixels, bHasVars ? " --vars " : "", bHasVars ? varsFile.string() : "");

	common::ExecutableResult result = common::RunExecutableInNewConsole(rGaeaExecutable, commandLine);

	if (result.miExitCode != 0)
	{
		throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\". Use /gaea2-diagnose to examine the log file for failures.", result.miExitCode, rIslandFolder.string()));
	}

	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(intermediatesDir / pcFile))
		{
			throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses the FloatRaw32 format and AmbientOcclusion uses UshortRaw16.", rIslandFolder.string(), pcFile, std::filesystem::path(pcFile).stem().string()));
		}
	}

	// Gaea's FloatRaw32 export is normalized [0,1] in conventional heightmap orientation: 0 = low
	// elevation, 1 = peak. Scale to meters then subtract the constant beach offset, so the math
	// reduces to (pixel * elevationMeters) - kfBeachHeightMeters. On-disk bytes are engine-ready:
	// beach = 0 (where Gaea's water surface sits, since PatchArchetypeSeaLevel writes Level =
	// kfBeachHeightMeters / elevationMeters), ocean = negative (down to -kfBeachHeightMeters at
	// Gaea-normalized 0.0), land = positive (up to elevationMeters - kfBeachHeightMeters at
	// Gaea-normalized 1.0). NaN/Inf scrubbed because R32_SFLOAT is unbounded — any stray non-finite
	// pixel would poison the elevation G-buffer and vertex displacement; non-finite maps to the
	// island's sea floor.
	std::filesystem::path elevationFile = intermediatesDir / "Elevation.r32";
	std::vector<float> sourcePixels(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
	int64_t iExpectedBytes = static_cast<int64_t>(sourcePixels.size() * sizeof(float));
	int64_t iActualBytes = static_cast<int64_t>(std::filesystem::file_size(elevationFile));
	if (iActualBytes != iExpectedBytes)
	{
		throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} float32). Verify the archetype's Elevation Export node uses FloatRaw32 format and is unconstrained by an internal resolution override.", elevationFile.string(), iActualBytes, iExpectedBytes, iTexturePixels, iTexturePixels));
	}
	{
		std::ifstream readStream(elevationFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(sourcePixels.data()), iExpectedBytes);
	}
	for (float& rfPixel : sourcePixels)
	{
		rfPixel = std::isfinite(rfPixel) ? rfPixel * dimensions.fElevationMeters - kfBeachHeightMeters : -kfBeachHeightMeters;
	}

	// Auto-crop to the bbox of pixels above the sea-floor cut line. Cut line = `-kfBeachHeightMeters
	// + kfCropEpsilonAboveSeaFloorMeters`, i.e., epsilon meters up from the sea floor; everything
	// at or below that depth is trim. Empty bbox is a configuration error: the archetype produced
	// no terrain even one epsilon above the sea floor, so the island would be invisible.
	float fCropCutLineMeters = -kfBeachHeightMeters + kfCropEpsilonAboveSeaFloorMeters;
	int64_t iMinX = iTexturePixels;
	int64_t iMinY = iTexturePixels;
	int64_t iMaxX = -1;
	int64_t iMaxY = -1;
	for (int64_t iY = 0; iY < iTexturePixels; ++iY)
	{
		const float* pfRow = &sourcePixels.at(static_cast<size_t>(iY) * static_cast<size_t>(iTexturePixels));
		for (int64_t iX = 0; iX < iTexturePixels; ++iX)
		{
			if (pfRow[iX] > fCropCutLineMeters)
			{
				if (iX < iMinX)
				{
					iMinX = iX;
				}
				if (iX > iMaxX)
				{
					iMaxX = iX;
				}
				if (iY < iMinY)
				{
					iMinY = iY;
				}
				if (iY > iMaxY)
				{
					iMaxY = iY;
				}
			}
		}
	}
	if (iMaxX < 0)
	{
		throw std::runtime_error(std::format("Island \"{}\" has no pixels above the sea-floor cut line ({:.2f} m, = -kfBeachHeightMeters + {:.2f}): the archetype produced no terrain even {:.2f} m above the sea floor. Raise Island.json's elevationMeters, edit kfBeachHeightMeters in BakeIslandIntermediates.cpp and rebuild DataPacker, or pick a smaller kfCropEpsilonAboveSeaFloorMeters.", rIslandFolder.string(), fCropCutLineMeters, kfCropEpsilonAboveSeaFloorMeters, kfCropEpsilonAboveSeaFloorMeters));
	}

	// Expand bbox symmetrically to a multiple of iAlignmentRequirement in each axis, clamped to the
	// image. When clamping at an edge consumes one side's padding, push the unused remainder onto
	// the opposite side so the final dimension still meets the alignment. iTexturePixels is itself
	// a multiple of iAlignmentRequirement (enforced above), so the worst case fits without overflow.
	auto ExpandSpan = [iTexturePixels, iAlignmentRequirement](int64_t iLo, int64_t iHi, int64_t& riStart, int64_t& riSize)
	{
		int64_t iSpan = iHi - iLo + 1;
		int64_t iRequired = ((iSpan + iAlignmentRequirement - 1) / iAlignmentRequirement) * iAlignmentRequirement;
		int64_t iPad = iRequired - iSpan;
		int64_t iPadLow = iPad / 2;
		int64_t iPadHigh = iPad - iPadLow;
		int64_t iStart = iLo - iPadLow;
		int64_t iEnd = iHi + iPadHigh;
		if (iStart < 0)
		{
			iEnd += -iStart;
			iStart = 0;
		}
		if (iEnd > iTexturePixels - 1)
		{
			iStart -= (iEnd - (iTexturePixels - 1));
			iEnd = iTexturePixels - 1;
		}
		riStart = iStart;
		riSize = iEnd - iStart + 1;
	};

	int64_t iCropX = 0;
	int64_t iCropY = 0;
	int64_t iCropWidth = 0;
	int64_t iCropHeight = 0;
	ExpandSpan(iMinX, iMaxX, iCropX, iCropWidth);
	ExpandSpan(iMinY, iMaxY, iCropY, iCropHeight);
	LOG(kDefault, kDebug, "Cropping island \"{}\": bbox ({}..{},{}..{}) -> ({}+{},{}+{}) [aligned to {}]", rIslandFolder.string(), iMinX, iMaxX, iMinY, iMaxY, iCropX, iCropWidth, iCropY, iCropHeight, iAlignmentRequirement);

	// Crop elevation in-memory from texturePixels² to iCropWidth × iCropHeight, then box-filter
	// downsample by kiElevationDivisor and rewrite Elevation.r32. Elevation is sampled by a vertex
	// grid in Terrain.vert and doesn't need color-level resolution; this is the primary disk-size
	// lever for islands. Downsampled dims are also multiples of 4 because crop dims are multiples
	// of 4 × kiElevationDivisor.
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
				const float* pfRow = &sourcePixels.at(static_cast<size_t>(iCropY + iOutY * kiElevationDivisor + iDy) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX + iOutX * kiElevationDivisor));
				for (int64_t iDx = 0; iDx < kiElevationDivisor; ++iDx)
				{
					fSum += pfRow[iDx];
				}
			}
			downsampledPixels.at(static_cast<size_t>(iOutY) * static_cast<size_t>(iElevationWidth) + static_cast<size_t>(iOutX)) = fSum * fOneOverBoxSize;
		}
	}

	{
		std::ofstream writeStream(elevationFile, std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(downsampledPixels.data()), downsampledPixels.size() * sizeof(float));
	}

	// Crop AmbientOcclusion.r16 in lockstep with the elevation bbox. Color.exr and Normals.exr
	// stay full-res on disk (OpenEXR core in Texture.cpp is read-only); ExportIsland crops their
	// pixel data in-memory after Texture loads them, so the final BC outputs and JPG sidecars
	// land at the cropped dims.
	std::filesystem::path ambientOcclusionFile = intermediatesDir / "AmbientOcclusion.r16";
	{
		int64_t iExpectedAoBytes = iTexturePixels * iTexturePixels * static_cast<int64_t>(sizeof(uint16_t));
		int64_t iActualAoBytes = static_cast<int64_t>(std::filesystem::file_size(ambientOcclusionFile));
		if (iActualAoBytes != iExpectedAoBytes)
		{
			throw std::runtime_error(std::format("Gaea produced \"{}\" at {} bytes; expected {} bytes ({}x{} uint16). Verify the archetype's AmbientOcclusion Export node uses UshortRaw16 format.", ambientOcclusionFile.string(), iActualAoBytes, iExpectedAoBytes, iTexturePixels, iTexturePixels));
		}
		std::vector<uint16_t> aoFull(static_cast<size_t>(iTexturePixels) * static_cast<size_t>(iTexturePixels));
		{
			std::ifstream readStream(ambientOcclusionFile, std::ios::binary);
			readStream.read(reinterpret_cast<char*>(aoFull.data()), iExpectedAoBytes);
		}
		std::vector<uint16_t> aoCropped(static_cast<size_t>(iCropWidth) * static_cast<size_t>(iCropHeight));
		for (int64_t iY = 0; iY < iCropHeight; ++iY)
		{
			const uint16_t* puiSrc = &aoFull.at(static_cast<size_t>(iCropY + iY) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iCropX));
			uint16_t* puiDst = &aoCropped.at(static_cast<size_t>(iY) * static_cast<size_t>(iCropWidth));
			std::memcpy(puiDst, puiSrc, static_cast<size_t>(iCropWidth) * sizeof(uint16_t));
		}
		std::ofstream writeStream(ambientOcclusionFile, std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(aoCropped.data()), aoCropped.size() * sizeof(uint16_t));
	}

	// Parse Mesh.gltf (Mesher's glTF separate-format manifest; references Mesh.bin) and write
	// MeshProcessed.bin: a flat [int32 vertexCount, int32 indexCount, float3 positions[], uint32 indices[]]
	// blob in island-local meters. glTF 2.0 mandates right-handed Y-up: Mesher emits
	// (X_east, Y_height, Z_south) — height is in component 1, and gltf.z increases southward, so
	// we map engine Y = -gltf.z to make engine Y increase northward (engine convention).
	// Combined with engine Z = gltf.y, the mapping (X,Y,Z)=(gltf.x, -gltf.z, gltf.y) has determinant
	// +1 (swap-then-negate = two odd ops = even composition), so handedness is preserved and
	// triangle winding stays CCW-front in the engine frame. Gaea emits horizontal positions already
	// centered around (0, 0) (range [-footprintMeters/2, +footprintMeters/2]); height gets the
	// constant beach offset (kfBeachHeightMeters) subtracted so sea level = 0 (matches the elevation
	// pipeline convention). UVs (TEXCOORD_0) are discarded — runtime derives visible-area UV from
	// world XY. Indices are upcast to uint32 for uniform handling. After the glTF is parsed, an
	// adaptive beach-band subdivision pass densifies triangles whose Z-range overlaps the band
	// [kfBeachSubdivisionMinMeters, kfBeachSubdivisionMaxMeters] (straddling beach Z=0) until
	// longest XY edge <= kfBeachSubdivisionMaxEdgeMeters; out-of-band neighbors absorb the
	// resulting T-junctions via minimal 1->2 / 1->3 / 1->4 splits that introduce no new midpoints
	// (one-ring cascade firewall). Output layout is unchanged -- the new vertices/indices are just
	// appended to the same arrays before write.
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
		std::vector<float> meshPositions(static_cast<size_t>(iVertexCount) * 3);
		{
			const std::byte* pSrc = reinterpret_cast<const std::byte*>(rPosBuffer.data.data()) + rPosView.byteOffset + rPosAccessor.byteOffset;
			size_t uiStride = rPosAccessor.ByteStride(rPosView);
			for (int64_t iVertex = 0; iVertex < iVertexCount; ++iVertex)
			{
				const float* pfXyz = reinterpret_cast<const float*>(pSrc + static_cast<size_t>(iVertex) * uiStride);
				float fX = pfXyz[0];
				float fY = -pfXyz[2];
				float fZ = std::isfinite(pfXyz[1]) ? pfXyz[1] - kfBeachHeightMeters : -kfBeachHeightMeters;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 0] = fX;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 1] = fY;
				meshPositions[static_cast<size_t>(iVertex) * 3 + 2] = fZ;
			}
		}

		const tinygltf::Accessor& rIdxAccessor = gltfModel.accessors.at(static_cast<size_t>(rPrimitive.indices));
		const tinygltf::BufferView& rIdxView = gltfModel.bufferViews.at(static_cast<size_t>(rIdxAccessor.bufferView));
		const tinygltf::Buffer& rIdxBuffer = gltfModel.buffers.at(static_cast<size_t>(rIdxView.buffer));
		int64_t iIndexCount = static_cast<int64_t>(rIdxAccessor.count);
		std::vector<uint32_t> meshIndices(static_cast<size_t>(iIndexCount));
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
		// parent's range.
		//
		// Band is in absolute engine-meters around beach Z = 0 (independent of elevationMeters);
		// the two bounds can be tuned asymmetrically to widen the underwater or above-water side.
		float fBandMinZ = kfBeachSubdivisionMinMeters;
		float fBandMaxZ = kfBeachSubdivisionMaxMeters;
		int64_t iDepthCapHits = 0;
		{
			auto EdgeKey = [](uint32_t iA, uint32_t iB) -> uint64_t
			{
				uint32_t iMin = std::min(iA, iB);
				uint32_t iMax = std::max(iA, iB);
				return (static_cast<uint64_t>(iMin) << 32) | static_cast<uint64_t>(iMax);
			};

			auto VertexX = [&meshPositions](uint32_t iV) -> float
			{
				return meshPositions[static_cast<size_t>(iV) * 3 + 0];
			};
			auto VertexY = [&meshPositions](uint32_t iV) -> float
			{
				return meshPositions[static_cast<size_t>(iV) * 3 + 1];
			};
			auto VertexZ = [&meshPositions](uint32_t iV) -> float
			{
				return meshPositions[static_cast<size_t>(iV) * 3 + 2];
			};

			std::unordered_map<uint64_t, uint32_t> edgeMidpoints;
			std::unordered_map<uint64_t, std::array<int32_t, 2>> edgeTriangles;
			std::deque<uint32_t> worklist;
			std::vector<uint8_t> triangleAlive(static_cast<size_t>(iInitialTriangleCount), 1);
			std::vector<uint8_t> triangleDepth(static_cast<size_t>(iInitialTriangleCount), 0);

			auto EdgeAdd = [&edgeTriangles](uint64_t iKey, int32_t iTriangle)
			{
				auto it = edgeTriangles.try_emplace(iKey, std::array<int32_t, 2>{-1, -1}).first;
				std::array<int32_t, 2>& rSlots = it->second;
				if (rSlots[0] < 0)
				{
					rSlots[0] = iTriangle;
					return;
				}
				if (rSlots[1] < 0)
				{
					rSlots[1] = iTriangle;
					return;
				}
				ASSERT(false);  // 3+ triangles share a single edge -- malformed mesh input.
			};

			auto EdgeRemove = [&edgeTriangles](uint64_t iKey, int32_t iTriangle)
			{
				auto it = edgeTriangles.find(iKey);
				if (it == edgeTriangles.end())
				{
					return;
				}
				std::array<int32_t, 2>& rSlots = it->second;
				if (rSlots[0] == iTriangle)
				{
					rSlots[0] = -1;
				}
				if (rSlots[1] == iTriangle)
				{
					rSlots[1] = -1;
				}
				if (rSlots[0] < 0 && rSlots[1] < 0)
				{
					edgeTriangles.erase(it);
				}
			};

			auto EdgeOther = [&edgeTriangles](uint64_t iKey, int32_t iTriangle) -> int32_t
			{
				auto it = edgeTriangles.find(iKey);
				if (it == edgeTriangles.end())
				{
					return -1;
				}
				const std::array<int32_t, 2>& rSlots = it->second;
				if (rSlots[0] == iTriangle)
				{
					return rSlots[1];
				}
				if (rSlots[1] == iTriangle)
				{
					return rSlots[0];
				}
				return -1;
			};

			// Build initial edge adjacency.
			for (int64_t iTriangle = 0; iTriangle < iInitialTriangleCount; ++iTriangle)
			{
				uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
				uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
				uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
				EdgeAdd(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
				EdgeAdd(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
				EdgeAdd(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));
			}

			auto IsBandEligible = [&](uint32_t iA, uint32_t iB, uint32_t iC) -> bool
			{
				float fMinZ = std::min({VertexZ(iA), VertexZ(iB), VertexZ(iC)});
				float fMaxZ = std::max({VertexZ(iA), VertexZ(iB), VertexZ(iC)});
				return fMinZ <= fBandMaxZ && fMaxZ >= fBandMinZ;
			};

			auto LongestEdgeXY = [&](uint32_t iA, uint32_t iB, uint32_t iC) -> float
			{
				auto LengthSquared = [&](uint32_t iU, uint32_t iV) -> float
				{
					float fDx = VertexX(iV) - VertexX(iU);
					float fDy = VertexY(iV) - VertexY(iU);
					return fDx * fDx + fDy * fDy;
				};
				float fMaxSquared = std::max({LengthSquared(iA, iB), LengthSquared(iB, iC), LengthSquared(iC, iA)});
				return std::sqrt(fMaxSquared);
			};

			auto AddVertex = [&meshPositions](float fX, float fY, float fZ) -> uint32_t
			{
				uint32_t iNewIndex = static_cast<uint32_t>(meshPositions.size() / 3);
				meshPositions.push_back(fX);
				meshPositions.push_back(fY);
				meshPositions.push_back(fZ);
				return iNewIndex;
			};

			auto GetOrCreateMidpoint = [&](uint32_t iA, uint32_t iB) -> uint32_t
			{
				uint64_t iKey = EdgeKey(iA, iB);
				auto it = edgeMidpoints.find(iKey);
				if (it != edgeMidpoints.end())
				{
					return it->second;
				}
				float fMidX = 0.5f * (VertexX(iA) + VertexX(iB));
				float fMidY = 0.5f * (VertexY(iA) + VertexY(iB));
				float fMidZ = 0.5f * (VertexZ(iA) + VertexZ(iB));
				uint32_t iMidpoint = AddVertex(fMidX, fMidY, fMidZ);
				edgeMidpoints.emplace(iKey, iMidpoint);
				return iMidpoint;
			};

			auto LookupMidpoint = [&](uint32_t iA, uint32_t iB) -> uint32_t
			{
				auto it = edgeMidpoints.find(EdgeKey(iA, iB));
				return it != edgeMidpoints.end() ? it->second : UINT32_MAX;
			};

			auto AppendTriangle = [&](uint32_t iA, uint32_t iB, uint32_t iC, uint8_t iDepth) -> uint32_t
			{
				uint32_t iNewTriangle = static_cast<uint32_t>(meshIndices.size() / 3);
				meshIndices.push_back(iA);
				meshIndices.push_back(iB);
				meshIndices.push_back(iC);
				triangleAlive.push_back(1);
				triangleDepth.push_back(iDepth);
				EdgeAdd(EdgeKey(iA, iB), static_cast<int32_t>(iNewTriangle));
				EdgeAdd(EdgeKey(iB, iC), static_cast<int32_t>(iNewTriangle));
				EdgeAdd(EdgeKey(iC, iA), static_cast<int32_t>(iNewTriangle));
				return iNewTriangle;
			};

			auto KillTriangle = [&](uint32_t iTriangle)
			{
				uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
				uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
				uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
				EdgeRemove(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
				EdgeRemove(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
				EdgeRemove(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));
				triangleAlive[iTriangle] = 0;
				meshIndices[static_cast<size_t>(iTriangle) * 3 + 0] = UINT32_MAX;
				meshIndices[static_cast<size_t>(iTriangle) * 3 + 1] = UINT32_MAX;
				meshIndices[static_cast<size_t>(iTriangle) * 3 + 2] = UINT32_MAX;
			};

			// Seed worklist with every initial triangle that is in-band AND oversized.
			for (int64_t iTriangle = 0; iTriangle < iInitialTriangleCount; ++iTriangle)
			{
				uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
				uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
				uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];
				if (IsBandEligible(iA, iB, iC) && LongestEdgeXY(iA, iB, iC) > kfBeachSubdivisionMaxEdgeMeters)
				{
					worklist.push_back(static_cast<uint32_t>(iTriangle));
				}
			}

			while (!worklist.empty())
			{
				uint32_t iTriangle = worklist.front();
				worklist.pop_front();
				if (triangleAlive[iTriangle] == 0)
				{
					continue;
				}
				if (triangleDepth[iTriangle] >= kiBeachSubdivisionMaxDepth)
				{
					++iDepthCapHits;
					continue;
				}

				uint32_t iA = meshIndices[static_cast<size_t>(iTriangle) * 3 + 0];
				uint32_t iB = meshIndices[static_cast<size_t>(iTriangle) * 3 + 1];
				uint32_t iC = meshIndices[static_cast<size_t>(iTriangle) * 3 + 2];

				bool bBandTrigger = IsBandEligible(iA, iB, iC) && LongestEdgeXY(iA, iB, iC) > kfBeachSubdivisionMaxEdgeMeters;
				uint32_t iMidpointAB = LookupMidpoint(iA, iB);
				uint32_t iMidpointBC = LookupMidpoint(iB, iC);
				uint32_t iMidpointCA = LookupMidpoint(iC, iA);
				int32_t iExistingMidpoints = (iMidpointAB != UINT32_MAX) + (iMidpointBC != UINT32_MAX) + (iMidpointCA != UINT32_MAX);

				if (!bBandTrigger && iExistingMidpoints == 0)
				{
					continue;
				}

				uint8_t iChildDepth = static_cast<uint8_t>(triangleDepth[iTriangle] + 1);

				if (bBandTrigger)
				{
					// In-band 1->4 split. Capture neighbors BEFORE mutating edge tables so we know
					// who to notify on each parent edge. Create midpoints for any edges that don't
					// already have one; those new midpoints are what neighbors will absorb.
					int32_t iNeighborAB = EdgeOther(EdgeKey(iA, iB), static_cast<int32_t>(iTriangle));
					int32_t iNeighborBC = EdgeOther(EdgeKey(iB, iC), static_cast<int32_t>(iTriangle));
					int32_t iNeighborCA = EdgeOther(EdgeKey(iC, iA), static_cast<int32_t>(iTriangle));

					if (iMidpointAB == UINT32_MAX)
					{
						iMidpointAB = GetOrCreateMidpoint(iA, iB);
					}
					if (iMidpointBC == UINT32_MAX)
					{
						iMidpointBC = GetOrCreateMidpoint(iB, iC);
					}
					if (iMidpointCA == UINT32_MAX)
					{
						iMidpointCA = GetOrCreateMidpoint(iC, iA);
					}

					KillTriangle(iTriangle);

					uint32_t iChild0 = AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
					uint32_t iChild1 = AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
					uint32_t iChild2 = AppendTriangle(iMidpointCA, iMidpointBC, iC, iChildDepth);
					uint32_t iChild3 = AppendTriangle(iMidpointAB, iMidpointBC, iMidpointCA, iChildDepth);

					worklist.push_back(iChild0);
					worklist.push_back(iChild1);
					worklist.push_back(iChild2);
					worklist.push_back(iChild3);

					if (iNeighborAB >= 0 && triangleAlive[static_cast<size_t>(iNeighborAB)] != 0)
					{
						worklist.push_back(static_cast<uint32_t>(iNeighborAB));
					}
					if (iNeighborBC >= 0 && triangleAlive[static_cast<size_t>(iNeighborBC)] != 0)
					{
						worklist.push_back(static_cast<uint32_t>(iNeighborBC));
					}
					if (iNeighborCA >= 0 && triangleAlive[static_cast<size_t>(iNeighborCA)] != 0)
					{
						worklist.push_back(static_cast<uint32_t>(iNeighborCA));
					}
					continue;
				}

				// Absorption split: 1->2, 1->3, or 1->4 depending on midpoint count. No new midpoints
				// are created, so neighbors of this triangle gain no new T-junctions -- cascade firewall.
				KillTriangle(iTriangle);

				if (iExistingMidpoints == 1)
				{
					if (iMidpointAB != UINT32_MAX)
					{
						AppendTriangle(iA, iMidpointAB, iC, iChildDepth);
						AppendTriangle(iMidpointAB, iB, iC, iChildDepth);
					}
					else if (iMidpointBC != UINT32_MAX)
					{
						AppendTriangle(iA, iB, iMidpointBC, iChildDepth);
						AppendTriangle(iA, iMidpointBC, iC, iChildDepth);
					}
					else
					{
						AppendTriangle(iA, iB, iMidpointCA, iChildDepth);
						AppendTriangle(iB, iC, iMidpointCA, iChildDepth);
					}
				}
				else if (iExistingMidpoints == 2)
				{
					if (iMidpointAB != UINT32_MAX && iMidpointBC != UINT32_MAX)
					{
						// Midpoints on A-B and B-C. Corner B is between them.
						AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
						AppendTriangle(iA, iMidpointAB, iMidpointBC, iChildDepth);
						AppendTriangle(iA, iMidpointBC, iC, iChildDepth);
					}
					else if (iMidpointBC != UINT32_MAX && iMidpointCA != UINT32_MAX)
					{
						// Midpoints on B-C and C-A. Corner C is between them.
						AppendTriangle(iMidpointBC, iC, iMidpointCA, iChildDepth);
						AppendTriangle(iB, iMidpointBC, iMidpointCA, iChildDepth);
						AppendTriangle(iA, iB, iMidpointCA, iChildDepth);
					}
					else
					{
						// Midpoints on A-B and C-A. Corner A is between them.
						AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
						AppendTriangle(iMidpointAB, iB, iMidpointCA, iChildDepth);
						AppendTriangle(iMidpointCA, iB, iC, iChildDepth);
					}
				}
				else
				{
					// All three edges have midpoints. True 1->4 using the existing midpoints.
					AppendTriangle(iA, iMidpointAB, iMidpointCA, iChildDepth);
					AppendTriangle(iMidpointAB, iB, iMidpointBC, iChildDepth);
					AppendTriangle(iMidpointCA, iMidpointBC, iC, iChildDepth);
					AppendTriangle(iMidpointAB, iMidpointBC, iMidpointCA, iChildDepth);
				}
				// Absorption children inherit the parent's out-of-band Z-range (Z-range of children
				// is a subset of the parent's), so they cannot trigger a band split themselves --
				// no need to push them. They will be re-pushed automatically if a future in-band
				// split creates a new midpoint on one of their edges (via the iNeighbor* lookups).
			}

			// Compact: drop dead triangles (UINT32_MAX sentinel indices).
			std::vector<uint32_t> compactedIndices;
			compactedIndices.reserve(meshIndices.size());
			for (size_t i = 0; i < meshIndices.size(); i += 3)
			{
				if (meshIndices[i] == UINT32_MAX)
				{
					continue;
				}
				compactedIndices.push_back(meshIndices[i + 0]);
				compactedIndices.push_back(meshIndices[i + 1]);
				compactedIndices.push_back(meshIndices[i + 2]);
			}
			meshIndices = std::move(compactedIndices);
			iVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
			iIndexCount = static_cast<int64_t>(meshIndices.size());
		}

		if (iDepthCapHits > 0)
		{
			LOG(kDefault, kWarning, "Mesh \"{}\": beach subdivision hit depth cap ({}) on {} triangle(s); largest input triangles may still exceed {:.2f}m edge target AND the mesh may contain T-junction cracks where capped absorption-needing triangles were skipped (raise kiBeachSubdivisionMaxDepth or split it into separate in-band / absorption caps if observed)", rIslandFolder.string(), kiBeachSubdivisionMaxDepth, iDepthCapHits, kfBeachSubdivisionMaxEdgeMeters);
		}
		LOG(kDefault, kDebug, "Mesh \"{}\": {} -> {} vertices, {} -> {} triangles after beach subdivision (band Z=[{:.2f}, {:.2f}]m, edge target {:.2f}m)", rIslandFolder.string(), iInitialVertexCount, iVertexCount, iInitialTriangleCount, iIndexCount / 3, fBandMinZ, fBandMaxZ, kfBeachSubdivisionMaxEdgeMeters);

		// Crop the mesh to the heightmap's cropped bbox and re-center mesh-local origin on the post-crop
		// center. Gaea emits the Mesher mesh at the full pre-crop archetype footprint (fFootprintMeters
		// square, centered at origin), but the heightmap is auto-cropped to its tight land bbox plus halo
		// padding -- the per-island quad.f4VertexRect at runtime is sized to the post-crop dimensions
		// (IslandHeader::fWorldFootprintXMeters/YMeters). Without this pass, mesh vertices outside the
		// crop project to world XY outside f4VertexRect, and Terrain.vert derives visible-area UV from
		// world XY -- so those vertices sample G-buffer regions belonging to neighbouring islands or
		// empty texture space. The bbox is computed in engine-meters: iCropX/iCropWidth map directly to
		// engine X (Gaea X-east, no flip); iCropY/iCropHeight map to engine Y with a sign flip because
		// engine Y is north-positive (engine_y = -gltf.z) and heightmap row 0 is the north edge.
		// Discard any triangle whose three vertices are all outside the bbox; keep partial-cross
		// triangles to preserve silhouette quality. After the index buffer is compacted, repack the
		// vertex buffer to drop orphans, then re-center XY of every surviving vertex on the post-crop
		// center.
		int64_t iDiscardedTriangles = 0;
		{
			const double dPixelsToMeters = static_cast<double>(dimensions.fFootprintMeters) / static_cast<double>(iTexturePixels);
			const float fCropMinX_m = static_cast<float>(static_cast<double>(iCropX)                          * dPixelsToMeters - 0.5 * dimensions.fFootprintMeters);
			const float fCropMaxX_m = static_cast<float>(static_cast<double>(iCropX + iCropWidth)             * dPixelsToMeters - 0.5 * dimensions.fFootprintMeters);
			const float fCropMaxY_m = static_cast<float>(0.5 * dimensions.fFootprintMeters - static_cast<double>(iCropY)               * dPixelsToMeters);
			const float fCropMinY_m = static_cast<float>(0.5 * dimensions.fFootprintMeters - static_cast<double>(iCropY + iCropHeight) * dPixelsToMeters);
			const float fCropCenterX_m = 0.5f * (fCropMinX_m + fCropMaxX_m);
			const float fCropCenterY_m = 0.5f * (fCropMinY_m + fCropMaxY_m);

			auto VertexOutside = [&meshPositions, fCropMinX_m, fCropMaxX_m, fCropMinY_m, fCropMaxY_m](uint32_t iV) -> bool
			{
				float fX = meshPositions[static_cast<size_t>(iV) * 3 + 0];
				float fY = meshPositions[static_cast<size_t>(iV) * 3 + 1];
				return fX < fCropMinX_m || fX > fCropMaxX_m || fY < fCropMinY_m || fY > fCropMaxY_m;
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
				meshPositions[iV * 3 + 0] -= fCropCenterX_m;
				meshPositions[iV * 3 + 1] -= fCropCenterY_m;
			}

			iVertexCount = static_cast<int64_t>(meshPositions.size() / 3);
			iIndexCount = static_cast<int64_t>(meshIndices.size());
			LOG(kDefault, kDebug, "Mesh \"{}\": cropped {} triangles outside heightmap bbox, re-centered XY by ({:.2f}, {:.2f})m, {} -> {} vertices", rIslandFolder.string(), iDiscardedTriangles, fCropCenterX_m, fCropCenterY_m, iOldVertexCount, iVertexCount);
		}

		std::filesystem::path meshProcessedFile = intermediatesDir / "MeshProcessed.bin";
		std::ofstream meshOut(meshProcessedFile, std::ios::binary | std::ios::trunc);
		int32_t iVertexCount32 = static_cast<int32_t>(iVertexCount);
		int32_t iIndexCount32 = static_cast<int32_t>(iIndexCount);
		meshOut.write(reinterpret_cast<const char*>(&iVertexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(&iIndexCount32), sizeof(int32_t));
		meshOut.write(reinterpret_cast<const char*>(meshPositions.data()), static_cast<std::streamsize>(meshPositions.size() * sizeof(float)));
		meshOut.write(reinterpret_cast<const char*>(meshIndices.data()), static_cast<std::streamsize>(meshIndices.size() * sizeof(uint32_t)));
	}

	// Stamp BakedDimensions.json BEFORE the bake-version sentinel so the sentinel-last invariant
	// holds: a clean version sentinel implies the dimensions sidecar (and all cropped intermediates)
	// are valid. ExportIsland reads BakedDimensions.json to size Texture ctors and to crop EXRs.
	{
		nlohmann::json bakedJson;
		bakedJson["widthMeters"] = dimensions.fFootprintMeters * static_cast<float>(iCropWidth) / static_cast<float>(iTexturePixels);
		bakedJson["heightMeters"] = dimensions.fFootprintMeters * static_cast<float>(iCropHeight) / static_cast<float>(iTexturePixels);
		bakedJson["elevationMeters"] = dimensions.fElevationMeters;
		bakedJson["cropX"] = iCropX;
		bakedJson["cropY"] = iCropY;
		bakedJson["cropWidth"] = iCropWidth;
		bakedJson["cropHeight"] = iCropHeight;
		bakedJson["fullTexturePixels"] = iTexturePixels;
		std::ofstream bakedStream(intermediatesDir / kpcBakedDimensionsFile);
		bakedStream << bakedJson.dump(4);
	}

	// Stamp the bake-version sentinel last, after every intermediate is written. A crash mid-bake
	// leaves the sentinel absent (or with the previous version), so IsBakeDirty will retry.
	{
		std::ofstream versionStream(intermediatesDir / kpcBakeVersionFile);
		versionStream << kiBakeVersion;
	}

	LOG(kDefault, kDebug, "Gaea bake succeeded for \"{}\"", rIslandFolder.string());
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

BakedDimensions ReadBakedDimensions(const std::filesystem::path& rIslandFolder)
{
	std::filesystem::path bakedJsonFile = rIslandFolder / kpcIslandIntermediatesDir / kpcBakedDimensionsFile;
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
