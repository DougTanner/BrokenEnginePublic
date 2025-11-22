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

	// Extract project name from project data directory and append to output directory
	std::filesystem::path normalizedPath = mpInputDirectories[1].lexically_normal();
	std::filesystem::path projectDataParent = normalizedPath.parent_path();
	mProjectName = projectDataParent.filename().string();

	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[0]));
	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[1]));
	std::filesystem::create_directories(mOutputDirectory);
	VERIFY_SUCCESS(std::filesystem::exists(mOutputDirectory));

	// Vulkan SDK Path
	char pcDirectory[MAX_PATH] {};
	DWORD result = GetEnvironmentVariable("VK_SDK_PATH", pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	if (result == 0)
	{
		throw std::runtime_error("VK_SDK_PATH environment variable not found");
	}
	mVulkanSdkBinariesDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mVulkanSdkBinariesDirectory));
	mVulkanSdkBinariesDirectory.append("Bin");
	LOG("Vulkan binaries directory: \"{}\"", gpFileManager->mVulkanSdkBinariesDirectory.string());

	// Input data directories
	LOG("Engine data directory: \"{}\"", mpInputDirectories[0].string());
	LOG("Game data directory: \"{}\"", mpInputDirectories[1].string());
	LOG("Project name: \"{}\"", mProjectName);

	// Temporaries directory
	DWORD tempResult = GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	if (tempResult == 0)
	{
		throw std::runtime_error("Failed to get temp directory path");
	}
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	mTempDirectory /= mProjectName;
	std::filesystem::create_directories(mTempDirectory);
	LOG("Temp directory: \"{}\"", gpFileManager->mTempDirectory.string());

	// Output file and directory
	if (!std::filesystem::exists(mOutputDirectory))
	{
		MessageBox(nullptr, mOutputDirectory.string().c_str(), "Output directory will be created", MB_OK | MB_SYSTEMMODAL);
		std::filesystem::create_directories(mOutputDirectory);
	}
	LOG("Output directory: \"{}\"", mOutputDirectory.string());
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
				LOG("\nCopying ThirdParty attribution files");
				LOG_INDENT(1);
				bAnyCopied = true;
			}

			std::filesystem::copy_file(rSourceFile, destinationFile, std::filesystem::copy_options::overwrite_existing);
			LOG("Copied: {}/{}", rLibraryName, rSourceFile.filename().string());
		}
	};

	for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(thirdPartyDirectory))
	{
		if (!rDirectoryEntry.is_directory())
		{
			continue;
		}

		std::string libraryName = rDirectoryEntry.path().filename().string();
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
		LOG_INDENT(-1);
	}
}
