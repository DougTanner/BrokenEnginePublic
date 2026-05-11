#include "BakeIslandIntermediates.h"

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

// Per-mip intermediates produced by each Gaea invocation. The .r16 entries are headerless
// linear unorm-16 (Gaea's UshortRaw16 format), precision-matched to the engine's R16_UNORM /
// BC4_UNORM destinations. Color/Normals stay multi-channel EXR.
constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r16",
	"Color.exr",
	"Elevation.r16",
	"Normals.exr",
};

// Default mip chain when island.json omits a "mips" array. Powers of two terminating at the
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

std::filesystem::path ResolveTerrain(const std::filesystem::path& rInputDirectory, const std::filesystem::path& rIslandFolder, const nlohmann::json& rIslandJson)
{
	// Named archetype: two-tier lookup — shared Islands/ folder, then sibling in the island folder.
	if (rIslandJson.contains("archetype") && rIslandJson["archetype"].is_string())
	{
		std::string archetype = rIslandJson["archetype"].get<std::string>();

		std::filesystem::path sharedPath = rInputDirectory / "Islands" / (archetype + ".terrain");
		if (std::filesystem::exists(sharedPath))
		{
			return sharedPath;
		}

		std::filesystem::path siblingPath = rIslandFolder / (archetype + ".terrain");
		if (std::filesystem::exists(siblingPath))
		{
			return siblingPath;
		}

		throw std::runtime_error(std::format("Archetype '{}' not found. Looked for: \"{}\" and \"{}\"", archetype, sharedPath.string(), siblingPath.string()));
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
		throw std::runtime_error(std::format("No .terrain file found in \"{}\". Add one to this folder or set an \"archetype\" key in island.json pointing at a shared archetype in Islands/.", rIslandFolder.string()));
	}
	if (matches.size() > 1)
	{
		std::string list;
		for (const std::filesystem::path& rPath : matches)
		{
			list += std::format("\n  {}", rPath.filename().string());
		}
		throw std::runtime_error(std::format("Multiple .terrain files found in \"{}\":{}\nSet an \"archetype\" key in island.json to choose one, or leave only one .terrain in the folder.", rIslandFolder.string(), list));
	}

	return matches.front();
}

bool IsBakeDirty(const std::filesystem::path& rIslandFolder, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile, const std::vector<int32_t>& rMips)
{
	std::filesystem::path worldFile = rIslandFolder / "world.json";
	if (!std::filesystem::exists(worldFile))
	{
		return true;
	}

	for (size_t i = 0; i < rMips.size(); ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		for (const char* pcFile : kpcIntermediateFiles)
		{
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
			if (std::filesystem::last_write_time(mipDir / pcFile) < inputNewest)
			{
				return true;
			}
		}
	}

	return false;
}

struct WorldDimensions
{
	float fWidthMeters = 0.0f;
	float fHeightMeters = 0.0f;
};

WorldDimensions ReadTerrainDimensions(const std::filesystem::path& rTerrainFile)
{
	std::ifstream terrainStream(rTerrainFile);
	nlohmann::json terrainJson = nlohmann::json::parse(terrainStream);
	terrainStream.close();

	// Layout: Assets["$values"][0].Terrain.{Width, Height}. Verified against Island-1x1.terrain.
	const nlohmann::json& rTerrain = terrainJson.at("Assets").at("$values").at(0).at("Terrain");
	return WorldDimensions
	{
		.fWidthMeters = rTerrain.at("Width").get<float>(),
		.fHeightMeters = rTerrain.at("Height").get<float>(),
	};
}

void BakeOne(const std::filesystem::path& rGaeaExe, const std::filesystem::path& rInputDirectory, const std::filesystem::path& rIslandFolder)
{
	std::filesystem::path islandJsonFile = rIslandFolder / "island.json";

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

	std::filesystem::path archetypeFile = ResolveTerrain(rInputDirectory, rIslandFolder, islandJson);

	if (!IsBakeDirty(rIslandFolder, islandJsonFile, archetypeFile, mips))
	{
		return;
	}

	WorldDimensions worldDimensions = ReadTerrainDimensions(archetypeFile);

	LOG(kDefault, kDebug, "Baking island intermediates: \"{}\" (archetype: \"{}\", seed: {}, mips: {}, {:.1f}m x {:.1f}m)", rIslandFolder.string(), archetypeFile.string(), iSeed, mips.size(), worldDimensions.fWidthMeters, worldDimensions.fHeightMeters);

	// Strip DataPacker-owned keys; remaining keys become Gaea variables.
	nlohmann::json varsJson = islandJson;
	varsJson.erase("archetype");
	varsJson.erase("seed");
	varsJson.erase("mips");

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

	for (size_t i = 0; i < mips.size(); ++i)
	{
		std::filesystem::path mipDir = rIslandFolder / std::format("mip{}", i);
		std::filesystem::create_directories(mipDir);

		// Gaea.Swarm.exe requires a real console for stdin/stdout/stderr — invoke via the
		// new-console helper rather than piped capture (the latter trips an IOException at
		// Gaea startup). argv[0] is the executable's own path so cmdline starts with the
		// quoted exe.
		std::wstring commandLine;
		commandLine += L"\"" + rGaeaExe.native() + L"\"";
		commandLine += L" --silent";
		commandLine += L" --Filename \"" + archetypeFile.native() + L"\"";
		commandLine += L" --buildpath \"" + mipDir.native() + L"\"";
		commandLine += std::format(L" --resolution {}", mips.at(i));
		commandLine += std::format(L" --seed {}", iSeed);
		if (bHasVars)
		{
			commandLine += L" --vars \"" + varsFile.native() + L"\"";
		}

		LOG(kDefault, kDebug, "Running: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {} --seed {}{}{}", rGaeaExe.string(), archetypeFile.string(), mipDir.string(), mips.at(i), iSeed, bHasVars ? " --vars " : "", bHasVars ? varsFile.string() : "");

		common::ExecutableResult result = common::RunExecutableInNewConsole(rGaeaExe, commandLine);

		if (result.miExitCode != 0)
		{
			throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\" (mip{}). Output isn't captured under the new-console invocation; re-run interactively to diagnose.", result.miExitCode, rIslandFolder.string(), i));
		}

		for (const char* pcFile : kpcIntermediateFiles)
		{
			if (!std::filesystem::exists(mipDir / pcFile))
			{
				throw std::runtime_error(std::format("Gaea bake for \"{}\" mip{} did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder, and that the AmbientOcclusion / Elevation Export nodes use the UshortRaw16 format.", rIslandFolder.string(), i, pcFile, std::filesystem::path(pcFile).stem().string()));
			}
		}
	}

	// Sidecar consumed by ExportIsland to stamp IslandHeader. Written after all mips succeed so a
	// partial bake leaves the island dirty rather than half-described.
	nlohmann::json worldJson;
	worldJson["widthMeters"] = worldDimensions.fWidthMeters;
	worldJson["heightMeters"] = worldDimensions.fHeightMeters;
	std::ofstream worldStream(rIslandFolder / "world.json");
	worldStream << worldJson.dump();
	worldStream.close();

	LOG(kDefault, kDebug, "Gaea bake succeeded for \"{}\"", rIslandFolder.string());
}

} // namespace

void BakeIslandIntermediates()
{
	std::vector<std::tuple<std::filesystem::path, std::filesystem::path>> islandFolders;
	for (const std::filesystem::path& rInputDirectory : gpFileManager->mpInputDirectories)
	{
		std::filesystem::path islandsRoot = rInputDirectory / "Islands";
		if (!std::filesystem::exists(islandsRoot))
		{
			continue;
		}

		for (const std::filesystem::directory_entry& rEntry : std::filesystem::directory_iterator(islandsRoot))
		{
			if (rEntry.is_directory() && std::filesystem::exists(rEntry.path() / "island.json"))
			{
				islandFolders.emplace_back(rInputDirectory, rEntry.path());
			}
		}
	}

	if (islandFolders.empty())
	{
		return;
	}

	std::sort(islandFolders.begin(), islandFolders.end());

	std::filesystem::path gaeaExe = ResolveGaeaExecutable();

	for (const auto& [rInputDirectory, rIslandFolder] : islandFolders)
	{
		BakeOne(gaeaExe, rInputDirectory, rIslandFolder);
	}
}
