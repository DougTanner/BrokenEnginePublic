#include "BakeIslandIntermediates.h"

#include "ExportJobs/ExportIsland.h"
#include "FileManager.h"

#pragma warning(push, 0)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#ifdef __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#endif
#include "tinygltf/json.hpp"
#ifdef __clang__
	#pragma clang diagnostic pop
#endif
#pragma warning(pop)

namespace
{

constexpr const wchar_t* kpwcGaeaDefaultPath = L"C:\\Program Files\\QuadSpinner\\Gaea 2\\Gaea.Swarm.exe";
constexpr const char* kpcGaeaEnvVar = "GAEA2_PATH";

// Per-mip intermediates produced by each Gaea invocation. Elevation.r32 is headerless IEEE-754
// float (Gaea's FloatRaw32 format), absolute meters with 0 at ocean bottom — DataPacker offsets
// by common::kfOceanDepthMeters at ingest. AmbientOcclusion.r16 stays UshortRaw16 (precision-
// matched to BC4_UNORM). Color/Normals stay multi-channel EXR.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.exr",
	"Elevation.r32",
	"Normals.exr",
};

// Default mip chain when Island.json omits a "mips" array. Powers of two terminating at the
// BC4/BC5/BC7 alignment floor (each level must remain a multiple of 4).
const std::vector<int32_t> kDefaultMips = {8192, 4096, 2048, 1024};

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
	if (rIslandJson.contains("archetype") && rIslandJson["archetype"].is_string())
	{
		std::string archetype = rIslandJson["archetype"].get<std::string>();

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

	// No archetype key: auto-discover a single sibling .terrain file in the island folder.
	std::vector<std::filesystem::path> matches;
	for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(rIslandFolder))
	{
		if (rEntry.is_regular_file() && rEntry.path().extension() == ".terrain")
		{
			matches.push_back(rEntry.path());
		}
	}

	if (matches.empty())
	{
		throw std::runtime_error(std::format("No .terrain file found in \"{}\". Add one to this folder or set an \"archetype\" key in Island.json pointing at a shared archetype in Islands/.", rIslandFolder.string()));
	}
	if (matches.size() > 1)
	{
		std::string list;
		for (const std::filesystem::path& rPath : matches)
		{
			list += std::format("\n  {}", rPath.filename().string());
		}
		throw std::runtime_error(std::format("Multiple .terrain files found in \"{}\":{}\nSet an \"archetype\" key in Island.json to choose one, or leave only one .terrain in the folder.", rIslandFolder.string(), list));
	}

	return matches.front();
}

// Elevation is only baked from `iElevationStart` onward (mips[0..start) carry color/AO/normals
// only — see kiElevationDivisor). Existence + timestamp checks must skip Elevation.r32 below that
// index, otherwise the dirty check would forever re-trigger.
bool IsBakeDirty(const std::filesystem::path& rIslandFolder, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile, const std::vector<int32_t>& rMips, size_t iElevationStart)
{
	for (size_t i = 0; i < rMips.size(); ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (i < iElevationStart && std::string_view(pcFile) == "Elevation.r32")
			{
				continue;
			}
			if (!std::filesystem::exists(mipDir / pcFile))
			{
				return true;
			}
		}
	}

	std::filesystem::file_time_type inputNewest = std::max(std::filesystem::last_write_time(rIslandJsonFile), std::filesystem::last_write_time(rArchetypeFile));
	for (size_t i = 0; i < rMips.size(); ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (i < iElevationStart && std::string_view(pcFile) == "Elevation.r32")
			{
				continue;
			}
			if (std::filesystem::last_write_time(mipDir / pcFile) < inputNewest)
			{
				return true;
			}
		}
	}

	return false;
}

WorldDimensions ReadTerrainDimensions(const std::filesystem::path& rTerrainFile)
{
	std::ifstream terrainStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(terrainStream);
	terrainStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	const nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	return WorldDimensions
	{
		.fFootprintMeters = rTerrain.at("Width").get<float>(),
		.fElevationMeters = rTerrain.at("Height").get<float>(),
	};
}

// Serializes patch+bake+restore against the shared archetype .terrain. Bakes are sequential today,
// so this is defense-in-depth — but a partially-patched archetype visible to a concurrent reader
// would silently produce wrong-sized heightmaps.
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

void PatchTerrainDimensions(const std::filesystem::path& rTerrainFile, const WorldDimensions& rDimensions)
{
	std::ifstream readStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(readStream);
	readStream.close();

	nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	rTerrain.at("Width") = rDimensions.fFootprintMeters;
	rTerrain.at("Height") = rDimensions.fElevationMeters;

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

	int32_t iSeed = islandJson.value("seed", 0);
	std::vector<int32_t> mips = islandJson.value("mips", kDefaultMips);

	for (int32_t iResolution : mips)
	{
		if (iResolution < 4 || (iResolution % 4) != 0)
		{
			throw std::runtime_error(std::format("\"{}\" mips entry {} is invalid: each level must be >= 4 and a multiple of 4 for BC alignment.", islandJsonFile.string(), iResolution));
		}
	}

	// Mip chain must strictly halve so ExportIsland's Texture::Export (which computes per-mip
	// dimensions by halving from mip 0) stays consistent with the actual authored sizes.
	for (size_t i = 1; i < mips.size(); ++i)
	{
		if (mips.at(i) != mips.at(i - 1) / 2)
		{
			throw std::runtime_error(std::format("\"{}\" mips array must strictly halve: mip{} is {} but mip{} is {} (expected {}).", islandJsonFile.string(), i, mips.at(i), i - 1, mips.at(i - 1), mips.at(i - 1) / 2));
		}
	}

	size_t iElevationStart = IslandElevationStartIndex(mips);
	if (iElevationStart >= mips.size())
	{
		throw std::runtime_error(std::format("\"{}\" mips array has no level <= mips[0] / {} ({}): elevation needs at least one mip at the reduced resolution.", islandJsonFile.string(), kiElevationDivisor, mips.at(0) / static_cast<int32_t>(kiElevationDivisor)));
	}

	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);

	// Migrate caches baked before the elevation/color ratio existed: drop Elevation.r16 from mips
	// below iElevationStart so IsBakeDirty / EnumerateMipDirs don't see stale full-resolution data.
	for (size_t i = 0; i < iElevationStart && i < mips.size(); ++i)
	{
		std::filesystem::path staleElevation = rIslandFolder / std::format("mip{}", i) / "Elevation.r32";
		if (std::filesystem::exists(staleElevation))
		{
			std::filesystem::remove(staleElevation);
			LOG(kDefault, kDebug, "Removed stale elevation intermediate: \"{}\"", staleElevation.string());
		}
	}

	// One-time migration: previous bakes emitted a `world.json` sidecar carrying width / height.
	// ExportIsland now reads those from Island.json (or .terrain) directly; the sidecar is stale
	// and confusing. Remove it whenever encountered.
	std::filesystem::path staleWorldJson = rIslandFolder / "world.json";
	if (std::filesystem::exists(staleWorldJson))
	{
		std::filesystem::remove(staleWorldJson);
		LOG(kDefault, kDebug, "Removed stale world.json sidecar: \"{}\"", staleWorldJson.string());
	}

	if (!IsBakeDirty(rIslandFolder, islandJsonFile, archetypeFile, mips, iElevationStart))
	{
		return;
	}

	LOG(kDefault, kDebug, "Baking island intermediates: \"{}\" (archetype: \"{}\", seed: {}, mips: {})", rIslandFolder.string(), archetypeFile.string(), iSeed, mips.size());

	// Strip DataPacker-owned keys; remaining keys become Gaea variables. widthMeters /
	// elevationMeters drive the archetype patch directly (Gaea's --vars can't reach
	// Terrain.{Width,Height}), so stripping them keeps the vars JSON to true graph variables.
	nlohmann::json varsJson = islandJson;
	varsJson.erase("archetype");
	varsJson.erase("seed");
	varsJson.erase("mips");
	varsJson.erase("widthMeters");
	varsJson.erase("elevationMeters");

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

	// Prune stale mip<N>/ folders from a prior bake with a longer chain — otherwise
	// ExportIsland::EnumerateMipDirs walks past mips.size() and ingests a too-long chain
	// with non-halving sizes.
	for (size_t i = mips.size(); ; ++i)
	{
		std::filesystem::path staleMipDir = rIslandFolder / std::format("mip{}", i);
		if (!std::filesystem::exists(staleMipDir))
		{
			break;
		}
		std::filesystem::remove_all(staleMipDir);
		LOG(kDefault, kDebug, "Removed stale mip directory: \"{}\"", staleMipDir.string());
	}

	// Gaea's --vars only injects values into named graph variable nodes; the archetype's intrinsic
	// Terrain.{Width,Height} aren't overridable that way. Patch the .terrain JSON in place around
	// the bake so Island.json widthMeters/elevationMeters actually drive the heightmap output. Lock
	// covers concurrent bakes (sequential today; future-proof) and the ScopedLambda restores raw
	// bytes + mtime so a successful bake is invisible to git and to IsBakeDirty.
	std::lock_guard<std::mutex> archetypeLock(gArchetypeMutex);

	std::optional<std::string> originalArchetypeBytes;
	std::optional<std::filesystem::file_time_type> originalArchetypeModificationTime;
	if (islandJson.contains("widthMeters") && islandJson.contains("elevationMeters"))
	{
		originalArchetypeBytes = ReadFileBytes(archetypeFile);
		originalArchetypeModificationTime = std::filesystem::last_write_time(archetypeFile);
		WorldDimensions overrideDimensions
		{
			.fFootprintMeters = islandJson.at("widthMeters").get<float>(),
			.fElevationMeters = islandJson.at("elevationMeters").get<float>(),
		};
		PatchTerrainDimensions(archetypeFile, overrideDimensions);
	}
	common::ScopedLambda restoreArchetype([&archetypeFile, &originalArchetypeBytes, &originalArchetypeModificationTime]()
	{
		if (originalArchetypeBytes)
		{
			WriteFileBytes(archetypeFile, *originalArchetypeBytes);
			std::filesystem::last_write_time(archetypeFile, *originalArchetypeModificationTime);
		}
	});

	// Read the archetype's current dimensions — post-patch if an override applied, otherwise the
	// intrinsic Terrain.{Width,Height}. fElevationMeters drives the per-pixel scale below: Gaea's
	// FloatRaw32 output is normalized [0,1] regardless of format, so we re-scale to absolute meters
	// using the same Terrain.Height that Gaea sees this bake.
	WorldDimensions effectiveDimensions = ReadTerrainDimensions(archetypeFile);

	for (size_t i = 0; i < mips.size(); ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		std::filesystem::create_directories(mipDir);

		// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the
		// new-console helper rather than piped capture (the latter trips an IOException at
		// Gaea startup). argv[0] is the executable's own path so cmdline starts with the
		// quoted exe.
		std::wstring commandLine;
		commandLine += L"\"" + rGaeaExecutable.native() + L"\"";
		commandLine += L" --silent";
		commandLine += L" --Filename \"" + archetypeFile.native() + L"\"";
		commandLine += L" --buildpath \"" + mipDir.native() + L"\"";
		commandLine += std::format(L" --resolution {}", mips.at(i));
		commandLine += std::format(L" --seed {}", iSeed);
		if (bHasVars)
		{
			commandLine += L" --vars \"" + varsFile.native() + L"\"";
		}

		LOG(kDefault, kDebug, "Running: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {} --seed {}{}{}", rGaeaExecutable.string(), archetypeFile.string(), mipDir.string(), mips.at(i), iSeed, bHasVars ? " --vars " : "", bHasVars ? varsFile.string() : "");

		common::ExecutableResult result = common::RunExecutableInNewConsole(rGaeaExecutable, commandLine);

		if (result.miExitCode != 0)
		{
			throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\" (mip{}). Output isn't captured under the new-console invocation; re-run interactively to diagnose.", result.miExitCode, rIslandFolder.string(), i));
		}

		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (!std::filesystem::exists(mipDir / pcFile))
			{
				throw std::runtime_error(std::format("Gaea bake for \"{}\" mip{} did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the Elevation Export node uses the FloatRaw32 format and AmbientOcclusion uses UshortRaw16.", rIslandFolder.string(), i, pcFile, std::filesystem::path(pcFile).stem().string()));
			}
		}

		// Drop Elevation.r32 at unused mips. Gaea still bakes it (archetype emits all four outputs
		// per resolution); the only way to skip in Gaea proper is to gate the Elevation Export node
		// in the archetype graph on a resolution variable. Disk savings now; CPU savings would
		// need that archetype change.
		if (i < iElevationStart)
		{
			std::filesystem::remove(mipDir / "Elevation.r32");
			continue;
		}

		// Gaea's FloatRaw32 export is normalized [0,1] (the format choice gives float precision but
		// not absolute meters). Scale by Terrain.Height to recover meters [0, Terrain.Height] with
		// 0 at ocean bottom, then subtract kfOceanDepthMeters so on-disk bytes are engine-ready
		// (beach = 0, ocean = negative). Single ingest point keeps the file the source of truth
		// for both CPU heightmap reads and GPU texture passthrough. NaN/Inf scrubbed because
		// R32_SFLOAT is unbounded (R16_UNORM previously guaranteed [0,1]) — any stray non-finite
		// pixel would poison the elevation G-buffer and vertex displacement.
		std::filesystem::path elevationFile = mipDir / "Elevation.r32";
		std::vector<float> pixels(static_cast<size_t>(mips.at(i)) * static_cast<size_t>(mips.at(i)));
		std::ifstream readStream(elevationFile, std::ios::binary);
		readStream.read(reinterpret_cast<char*>(pixels.data()), pixels.size() * sizeof(float));
		readStream.close();
		for (float& rfPixel : pixels)
		{
			float fScaled = std::isfinite(rfPixel) ? rfPixel * effectiveDimensions.fElevationMeters - common::kfOceanDepthMeters : -common::kfOceanDepthMeters;
			rfPixel = fScaled;
		}
		std::ofstream writeStream(elevationFile, std::ios::binary | std::ios::trunc);
		writeStream.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * sizeof(float));
		writeStream.close();
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

	// Island.json override path: both keys present → return as-is, no .terrain read needed.
	if (islandJson.contains("widthMeters") && islandJson.contains("elevationMeters"))
	{
		return WorldDimensions
		{
			.fFootprintMeters = islandJson.at("widthMeters").get<float>(),
			.fElevationMeters = islandJson.at("elevationMeters").get<float>(),
		};
	}

	// Fall back to the archetype .terrain's intrinsic dimensions.
	std::filesystem::path archetypeFile = ResolveTerrain(rIslandFolder, islandJson);
	return ReadTerrainDimensions(archetypeFile);
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
