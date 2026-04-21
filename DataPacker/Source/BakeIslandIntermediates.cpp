#include "BakeIslandIntermediates.h"

#include "FileManager.h"

#pragma warning(push)
#pragma warning(disable : 5311) // nlohmann::json 3.10.4 uses the pre-C++20 literal-operator-id form 'operator "" _json'
#include "tinygltf/json.hpp"
#pragma warning(pop)

namespace
{

constexpr int32_t kiGaeaResolution = 8192;
constexpr const wchar_t* kpwcGaeaDefaultPath = L"C:\\Program Files\\QuadSpinner\\Gaea 2\\Gaea.Swarm.exe";
constexpr const char* kpcGaeaEnvVar = "GAEA2_PATH";

constexpr const char* kpcIntermediateFiles[] =
{
	"AmbientOcclusion.r32",
	"Color.exr",
	"Elevation.r32",
	"Normals.exr",
};

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

bool IsBakeDirty(const std::filesystem::path& rIslandFolder, const std::filesystem::path& rIslandJsonFile, const std::filesystem::path& rArchetypeFile)
{
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rIslandFolder / pcFile))
		{
			return true;
		}
	}

	std::filesystem::file_time_type inputNewest = std::max(std::filesystem::last_write_time(rIslandJsonFile), std::filesystem::last_write_time(rArchetypeFile));
	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (std::filesystem::last_write_time(rIslandFolder / pcFile) < inputNewest)
		{
			return true;
		}
	}

	return false;
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

	std::filesystem::path archetypeFile = ResolveTerrain(rInputDirectory, rIslandFolder, islandJson);

	if (!IsBakeDirty(rIslandFolder, islandJsonFile, archetypeFile))
	{
		return;
	}

	LOG(kDefault, kDebug, "Baking island intermediates: \"{}\" (archetype: \"{}\", seed: {})", rIslandFolder.string(), archetypeFile.string(), iSeed);

	// Strip DataPacker-owned keys; remaining keys become Gaea variables.
	nlohmann::json varsJson = islandJson;
	varsJson.erase("archetype");
	varsJson.erase("seed");

	std::filesystem::path varsFile = gpFileManager->mTempDirectory / (rIslandFolder.filename().string() + ".gaea-vars.json");
	std::ofstream varsStream(varsFile);
	varsStream << varsJson.dump();
	varsStream.close();
	common::ScopedLambda varsFileCleanup([&varsFile] { std::filesystem::remove(varsFile); });

	// Launch via cmd.exe /c to isolate Gaea from DataPacker's in-process state (injected DLLs, CRT init quirks,
	// debugger attach side effects). cmd's "double-quote the whole payload" convention: if /c's argument
	// starts and ends with a quote, cmd strips those outer quotes and parses the middle as a normal command line.
	std::filesystem::path cmdExe = L"C:\\Windows\\System32\\cmd.exe";
	std::wstring commandLine;
	commandLine += L"\"" + cmdExe.native() + L"\""; // argv[0]: quoted cmd.exe path
	commandLine += L" /c \"";                       // /c + outer wrap open
	commandLine += L"\"" + rGaeaExe.native() + L"\"";
	commandLine += L" --silent";
	commandLine += L" --Filename \"" + archetypeFile.native() + L"\"";
	commandLine += L" --buildpath \"" + rIslandFolder.native() + L"\"";
	commandLine += std::format(L" --resolution {}", kiGaeaResolution);
	commandLine += std::format(L" --seed {}", iSeed);
	commandLine += L" --vars \"" + varsFile.native() + L"\"";
	commandLine += L"\"";                           // outer wrap close

	LOG(kDefault, kDebug, "Running via cmd.exe /c: \"{}\" --silent --Filename \"{}\" --buildpath \"{}\" --resolution {} --seed {} --vars \"{}\"", rGaeaExe.string(), archetypeFile.string(), rIslandFolder.string(), kiGaeaResolution, iSeed, varsFile.string());

	common::ExecutableResult result = common::RunExecutable(cmdExe, commandLine);

	if (result.miExitCode != 0)
	{
		throw std::runtime_error(std::format("Gaea.Swarm.exe exited with code {} for \"{}\":\n{}", result.miExitCode, rIslandFolder.string(), result.mOutput));
	}

	for (const char* pcFile : kpcIntermediateFiles)
	{
		if (!std::filesystem::exists(rIslandFolder / pcFile))
		{
			throw std::runtime_error(std::format("Gaea bake for \"{}\" did not produce \"{}\". Verify the archetype graph has an Export node named \"{}\" writing to Build Folder with \"Remove Primary port name\" enabled. Gaea output:\n{}", rIslandFolder.string(), pcFile, std::filesystem::path(pcFile).stem().string(), result.mOutput));
		}
	}

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
