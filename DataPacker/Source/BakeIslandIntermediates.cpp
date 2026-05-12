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
// [0,1] at bake time — DataPacker reads the Sea node's ShoreHeight from the .terrain archetype,
// then converts to engine-meters as ((1 - pixel) - ShoreHeight) * elevationMeters so beach = 0
// and the sea floor sits at -(ShoreHeight × elevationMeters). AmbientOcclusion.r16 stays
// UshortRaw16 (precision-matched to BC4_UNORM). Color/Normals stay multi-channel EXR.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.exr",
	"Elevation.r32",
	"Normals.exr",
};

// Bake formula/algorithm version. Bump whenever BakeOne's elevation transform, downsample, or
// any other code-only behavior of the bake changes (i.e., anything not captured by Island.json
// or archetype mtimes). IsBakeDirty reads `Intermediates/BakeVersion.txt` and forces a re-bake
// if the recorded version doesn't match; the bake writes the current version on success.
// History:
//   2 - Un-invert Gaea FloatRaw32+Mask pixels via (1 - pixel) before applying ShoreHeight offset.
//   1 - Initial scale+offset+downsample (legacy; never written to disk, treated as "missing").
constexpr int32_t kiBakeVersion = 2;
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

// Serializes patch+bake+restore against the shared archetype .terrain. Bakes are sequential today,
// so this is defense-in-depth — but a partially-patched archetype visible to a concurrent reader
// would silently produce wrong-sized heightmaps or wrong-seed terrain.
std::mutex gArchetypeMutex;

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
	std::filesystem::path tempFile = rFile;
	tempFile += L".tmp";
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

// Walks Terrain.Nodes for the Sea node and returns its normalized ShoreHeight (the beach line in
// Gaea's [0,1] elevation space). DataPacker uses this to position the engine-meters zero point at
// the beach line per-island: `pixel_meters = (pixel_normalized - shoreHeight) * elevationMeters`.
// Nodes is a dict keyed by node ID, so we scan values for the one whose $type starts with the
// Gaea Sea node prefix.
float ReadShoreHeight(const nlohmann::json& rTerrainJson)
{
	const nlohmann::json& rNodes = rTerrainJson.at("Assets").at("$values").at(0).at("Terrain").at("Nodes");
	for (const auto& [rKey, rNode] : rNodes.items())
	{
		if (!rNode.is_object() || !rNode.contains("$type"))
		{
			continue;
		}
		std::string type = rNode.at("$type").get<std::string>();
		if (type.starts_with("QuadSpinner.Gaea.Nodes.Sea"))
		{
			return rNode.at("ShoreHeight").get<float>();
		}
	}
	throw std::runtime_error("Archetype has no Sea node — DataPacker derives the beach line from its ShoreHeight to position the engine-meters zero. Add a Sea node to the graph or extend BakeIslandIntermediates to handle sea-less archetypes.");
}

// Patches the archetype in place (Width/Height + every per-node Seed) and returns the Sea node's
// ShoreHeight in one parse so the caller can use it for the elevation offset without re-reading
// the file.
float PatchArchetype(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions, int32_t iSeed)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	rTerrain.at("Width") = rDimensions.fFootprintMeters;
	rTerrain.at("Height") = rDimensions.fElevationMeters;

	PatchArchetypeSeeds(terrainJson, iSeed);

	float fShoreHeight = ReadShoreHeight(terrainJson);

	WriteFileBytes(rTerrainFile, terrainJson.dump(2));

	return fShoreHeight;
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

	// texturePixels drives BOTH the bake (full res) and the elevation downsample (texturePixels /
	// kiElevationDivisor). Constraint ensures both dimensions remain multiples of 4 for BC4/5/7
	// block alignment.
	int64_t iAlignmentRequirement = 4 * kiElevationDivisor;
	if (iTexturePixels < iAlignmentRequirement || (iTexturePixels % iAlignmentRequirement) != 0)
	{
		throw std::runtime_error(std::format("\"{}\" texturePixels {} is invalid: must be >= {} and a multiple of {} so that both texturePixels and texturePixels / {} are multiples of 4 for BC alignment.", islandJsonFile.string(), iTexturePixels, iAlignmentRequirement, iAlignmentRequirement, kiElevationDivisor));
	}

	int64_t iElevationResolution = iTexturePixels / kiElevationDivisor;

	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);

	if (!IsBakeDirty(rIslandFolder, islandJsonFile, archetypeFile))
	{
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
	// in-place around the bake, then restore byte-and-mtime-identical state on scope exit. Lock
	// covers concurrent bakes (sequential today; future-proof) and the ScopedLambda restore
	// guarantees a successful or failed bake leaves the archetype invisible to git and to
	// IsBakeDirty's mtime check.
	std::lock_guard<std::mutex> archetypeLock(gArchetypeMutex);

	std::string originalArchetypeBytes = ReadFileBytes(archetypeFile);
	std::filesystem::file_time_type originalArchetypeModificationTime = std::filesystem::last_write_time(archetypeFile);
	common::ScopedLambda restoreArchetype([&archetypeFile, &originalArchetypeBytes, &originalArchetypeModificationTime]()
	{
		WriteFileBytes(archetypeFile, originalArchetypeBytes);
		std::filesystem::last_write_time(archetypeFile, originalArchetypeModificationTime);
	});

	float fShoreHeight = PatchArchetype(archetypeFile, dimensions, iSeed);
	float fBeachOffsetMeters = fShoreHeight * dimensions.fElevationMeters;

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
		throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\". Output isn't captured under the new-console invocation; re-run interactively to diagnose.", result.miExitCode, rIslandFolder.string()));
	}

	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(intermediatesDir / pcFile))
		{
			throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses the FloatRaw32 format and AmbientOcclusion uses UshortRaw16.", rIslandFolder.string(), pcFile, std::filesystem::path(pcFile).stem().string()));
		}
	}

	// Gaea's FloatRaw32 export with the Elevation node set to "Mask" render intent is normalized
	// [0,1] but inverted vs. conventional heightmap orientation: high float == low elevation,
	// low float == high elevation (verified by Elevation.jpg sidecar: white pixels at sea floor,
	// black at peaks). Un-invert with (1 - pixel), then apply the per-island beach offset so the
	// math reduces to ((1 - pixel) - ShoreHeight) × elevationMeters. On-disk bytes are
	// engine-ready: beach = 0 (at Gaea-normalized 1 - ShoreHeight), ocean = negative (down to
	// -fBeachOffsetMeters at Gaea-normalized 1.0), land = positive (up to (1 - ShoreHeight) ×
	// elevationMeters at Gaea-normalized 0.0). ShoreHeight itself is read from the Sea node in
	// conventional space (small value = near low end / sea level), so the un-invert only applies
	// to the pixel data, not to ShoreHeight. NaN/Inf scrubbed because R32_SFLOAT is unbounded —
	// any stray non-finite pixel would poison the elevation G-buffer and vertex displacement;
	// non-finite maps to the island's sea floor.
	// Then box-filter downsample 4x4 -> 1 to iElevationResolution (texturePixels / kiElevationDivisor)
	// and rewrite Elevation.r32 in place: elevation is sampled by a vertex grid in Terrain.vert
	// and doesn't need color-level resolution; this is the primary disk-size lever for islands.
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
		rfPixel = std::isfinite(rfPixel) ? (1.0f - rfPixel) * dimensions.fElevationMeters - fBeachOffsetMeters : -fBeachOffsetMeters;
	}

	float fOneOverBoxSize = 1.0f / static_cast<float>(kiElevationDivisor * kiElevationDivisor);
	std::vector<float> downsampledPixels(static_cast<size_t>(iElevationResolution) * static_cast<size_t>(iElevationResolution));
	for (int64_t iOutY = 0; iOutY < iElevationResolution; ++iOutY)
	{
		for (int64_t iOutX = 0; iOutX < iElevationResolution; ++iOutX)
		{
			float fSum = 0.0f;
			for (int64_t iDy = 0; iDy < kiElevationDivisor; ++iDy)
			{
				const float* pfRow = &sourcePixels.at(static_cast<size_t>((iOutY * kiElevationDivisor + iDy)) * static_cast<size_t>(iTexturePixels) + static_cast<size_t>(iOutX * kiElevationDivisor));
				for (int64_t iDx = 0; iDx < kiElevationDivisor; ++iDx)
				{
					fSum += pfRow[iDx];
				}
			}
			downsampledPixels.at(static_cast<size_t>(iOutY) * static_cast<size_t>(iElevationResolution) + static_cast<size_t>(iOutX)) = fSum * fOneOverBoxSize;
		}
	}

	{
		std::ofstream writeStream(elevationFile, std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(downsampledPixels.data()), downsampledPixels.size() * sizeof(float));
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
