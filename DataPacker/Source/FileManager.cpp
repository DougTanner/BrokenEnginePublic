#include "FileManager.h"

FileManager::FileManager(std::span<char*> argvSpan)
{
	ASSERT(gpFileManager == nullptr);

	gpFileManager = this;

	if (argvSpan.size() == 1)
	{
		mpInputDirectories[0] = "../../../Engine/Data";
		mpInputDirectories[1] = "../../../Projects/BrokenEngineSandbox/Data";
		mOutputDirectory = "../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/Output/Data";
	}
	else
	{
		ASSERT(argvSpan.size() == 4);
		mpInputDirectories[0] = argvSpan[1];
		mpInputDirectories[1] = argvSpan[2];
		mOutputDirectory = argvSpan[3];
	}

	mpInputDirectories[0] = std::filesystem::canonical(mpInputDirectories[0]);
	mpInputDirectories[1] = std::filesystem::canonical(mpInputDirectories[1]);

	// Repo ThirdParty dir, derived from the canonical engine-data dir (<repo>/Engine/Data) rather than a fragile output-relative .. chain
	mThirdPartyDirectory = mpInputDirectories[0].parent_path().parent_path() / "ThirdParty";
	VERIFY_SUCCESS(std::filesystem::exists(mThirdPartyDirectory));

	// Extract project name from project data directory
	mProjectName = mpInputDirectories[1].parent_path().filename().string();
	std::filesystem::create_directories(mOutputDirectory);
	mOutputDirectory = std::filesystem::canonical(mOutputDirectory);

	// Input data directories
	LOG(kDefault, kDebug, "Engine data directory: \"{}\"", mpInputDirectories[0].string());
	LOG(kDefault, kDebug, "Game data directory: \"{}\"", mpInputDirectories[1].string());
	LOG(kDefault, kDebug, "Project name: \"{}\"", mProjectName);

	// Temporaries directory
	wchar_t pcDirectory[MAX_PATH] {};
	DWORD uiTempResult = GetTempPathW(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	if (uiTempResult == 0)
	{
		throw std::runtime_error("Failed to get temp directory path");
	}
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	mTempDirectory /= mProjectName;
	std::filesystem::create_directories(mTempDirectory);
	LOG(kDefault, kDebug, "Temp directory: \"{}\"", mTempDirectory.string());

	LOG(kDefault, kDebug, "Output directory: \"{}\"", mOutputDirectory.string());
}

FileManager::~FileManager()
{
	if (gpFileManager == this)
	{
		gpFileManager = nullptr;
	}
}
