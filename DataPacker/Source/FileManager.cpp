#include "FileManager.h"

FileManager::FileManager(std::span<char*> argvSpan)
{
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

	// Extract project name from project data directory
	mProjectName = mpInputDirectories[1].parent_path().filename().string();
	std::filesystem::create_directories(mOutputDirectory);
	VERIFY_SUCCESS(std::filesystem::exists(mOutputDirectory));

	// Vulkan SDK Path
	char pcDirectory[MAX_PATH] {};
	DWORD uiResult = GetEnvironmentVariable("VK_SDK_PATH", pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	if (uiResult == 0)
	{
		throw std::runtime_error("VK_SDK_PATH environment variable not found");
	}
	mVulkanSdkBinariesDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mVulkanSdkBinariesDirectory));
	mVulkanSdkBinariesDirectory.append("Bin");
	LOG(kDefault, kDebug, "Vulkan binaries directory: \"{}\"", gpFileManager->mVulkanSdkBinariesDirectory.string());

	// Input data directories
	LOG(kDefault, kDebug, "Engine data directory: \"{}\"", mpInputDirectories[0].string());
	LOG(kDefault, kDebug, "Game data directory: \"{}\"", mpInputDirectories[1].string());
	LOG(kDefault, kDebug, "Project name: \"{}\"", mProjectName);

	// Temporaries directory
	DWORD uiTempResult = GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	if (uiTempResult == 0)
	{
		throw std::runtime_error("Failed to get temp directory path");
	}
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	mTempDirectory /= mProjectName;
	std::filesystem::create_directories(mTempDirectory);
	LOG(kDefault, kDebug, "Temp directory: \"{}\"", gpFileManager->mTempDirectory.string());

	LOG(kDefault, kDebug, "Output directory: \"{}\"", mOutputDirectory.string());
}

FileManager::~FileManager()
{
	gpFileManager = nullptr;
}

void FileManager::CopyThirdPartyLicenses()
{
	std::filesystem::path thirdPartyDirectory = mOutputDirectory / "../../../../../../ThirdParty";
	std::filesystem::path attributionDirectory = mOutputDirectory / "../Attribution";
	std::filesystem::create_directories(attributionDirectory);
	bool bAnyCopied = false;

	auto CopyLicenseFile = [&bAnyCopied](const std::filesystem::path& rSourceFile, const std::filesystem::path& rLibraryAttributionDirectory, const std::string& rLibraryName)
	{
		std::filesystem::path destinationFile = rLibraryAttributionDirectory / rSourceFile.filename();
		bool bNeedsCopy = !std::filesystem::exists(destinationFile) || std::filesystem::last_write_time(rSourceFile) > std::filesystem::last_write_time(destinationFile);

		if (bNeedsCopy)
		{
			std::filesystem::create_directories(rLibraryAttributionDirectory);

			if (!bAnyCopied)
			{
				LOG(kDefault, kDebug, "\nCopying ThirdParty attribution files");
				LogIndent(1);
				bAnyCopied = true;
			}

			std::filesystem::copy_file(rSourceFile, destinationFile, std::filesystem::copy_options::overwrite_existing);
			LOG(kDefault, kDebug, "Copied: {}/{}", rLibraryName, rSourceFile.filename().string());
		}
	};

	for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(thirdPartyDirectory))
	{
		if (!rDirectoryEntry.is_directory())
		{
			continue;
		}

		std::string libraryName = rDirectoryEntry.path().filename().string();
		if (libraryName == "Prebuilts")
		{
			continue;
		}
		std::filesystem::path libraryAttributionDirectory = attributionDirectory / libraryName;

		// Priority 1: Look for primary license files (LICENSE, LICENSE.md, LICENSE.txt)
		bool bFoundLicense = false;
		std::filesystem::path primaryLicenseFile;
		for (const std::filesystem::directory_entry& rFileEntry : std::filesystem::directory_iterator(rDirectoryEntry.path()))
		{
			if (!rFileEntry.is_regular_file())
			{
				continue;
			}

			std::string filenameLower = common::ToLower(rFileEntry.path().filename().string());
			if (filenameLower == "license" || filenameLower == "license.md" || filenameLower == "license.txt")
			{
				primaryLicenseFile = rFileEntry.path();
				bFoundLicense = true;
				break;
			}
		}

		// If primary license found, copy it and skip fallback search
		if (bFoundLicense)
		{
			CopyLicenseFile(primaryLicenseFile, libraryAttributionDirectory, libraryName);
			continue;
		}

		// Fallback: Search for alternative license/attribution files (copying, readme, manual.md)
		for (const std::filesystem::directory_entry& rFileEntry : std::filesystem::directory_iterator(rDirectoryEntry.path()))
		{
			if (!rFileEntry.is_regular_file())
			{
				continue;
			}

			std::string filenameLower = common::ToLower(rFileEntry.path().filename().string());
			if (filenameLower.find("copying") != std::string::npos || filenameLower == "manual.md" || filenameLower.find("readme") != std::string::npos)
			{
				bFoundLicense = true;
				CopyLicenseFile(rFileEntry.path(), libraryAttributionDirectory, libraryName);
			}
		}

		ASSERT(bFoundLicense);
	}

	if (bAnyCopied)
	{
		LogIndent(-1);
	}
}
