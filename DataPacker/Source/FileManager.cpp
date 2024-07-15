#include "FileManager.h"

FileManager::FileManager(std::span<char*> argvSpan)
{
	gpFileManager = this;

	ASSERT(argvSpan.size() == 4);
	mpInputDirectories[0] = argvSpan[1];
	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[0]));
	mpInputDirectories[1] = argvSpan[2];
	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[1]));
	std::filesystem::path outputDirectory = argvSpan[3];
	VERIFY_SUCCESS(std::filesystem::exists(outputDirectory));

	mDataHeader = outputDirectory;
	mDataHeader /= "Data.h";
	mDataFile = outputDirectory;
	mDataFile /= "Data.bin";
	mTexturesFile = outputDirectory;
	mTexturesFile /= "Textures.bin";

	char pcDirectory[MAX_PATH] {};

	// Windows SDK Path
	std::filesystem::path windowsSdkRootDirectory("C:\\Program Files (x86)\\Windows Kits\\10\\bin");
	if (!std::filesystem::exists(windowsSdkRootDirectory))
	{
		LOG("Checking for Windows SDK from: \"{}\"", "SOFTWARE\\WOW6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0");
		windowsSdkRootDirectory = common::GetStringValueFromHKLM(L"SOFTWARE\\WOW6432Node\\Microsoft\\Microsoft SDKs\\Windows\\v10.0", L"InstallationFolder");
		LOG("  Found: \"{}\"", windowsSdkRootDirectory.string());
		
		windowsSdkRootDirectory.append("bin");
	}

	LOG("Searching for Windows SDK binaries in: \"{}\"", windowsSdkRootDirectory.string());
	int64_t iHighestVersion = 0;
	for (const std::filesystem::directory_entry& rDirectoryEntry : std::filesystem::directory_iterator(windowsSdkRootDirectory))
	{
		if (!rDirectoryEntry.is_directory())
		{
			continue;
		}

		if (rDirectoryEntry.path().string().find("10.0.19041.0") != std::string::npos)
		{
			iHighestVersion = 19041;
			mWindowsSdkBinariesDirectory = rDirectoryEntry.path();
			break;
		}

		std::vector<std::string> split = common::Split(rDirectoryEntry.path().stem().string(), std::string("."));
		int64_t iVersion = std::atoi(split.back().c_str());
		if (iVersion > iHighestVersion)
		{
			iHighestVersion = iVersion;
			mWindowsSdkBinariesDirectory = rDirectoryEntry.path();
		}
	}

	ASSERT(iHighestVersion > 0);

	mWindowsSdkBinariesDirectory.append("x64");
	LOG("    Found: \"{}\"", gpFileManager->mWindowsSdkBinariesDirectory.string());

	// Vulkan SDK Path
	GetEnvironmentVariable("VK_SDK_PATH", pcDirectory, static_cast<DWORD>(std::size(pcDirectory) - 1));
	mVulkanSdkBinariesDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mVulkanSdkBinariesDirectory));
	mVulkanSdkBinariesDirectory.append("Bin");
	LOG("Vulkan binaries directory: \"{}\"", gpFileManager->mVulkanSdkBinariesDirectory.string());

	// Input data directories
	LOG("Engine data directory: \"{}\"", mpInputDirectories[0].string());
	LOG("Game data directory: \"{}\"", mpInputDirectories[1].string());

	// Temporaries directory
	GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	std::filesystem::create_directories(mTempDirectory);
	LOG("Temp directory: \"{}\"", gpFileManager->mTempDirectory.string());

	mDataFileTemp = mTempDirectory;
	mDataFileTemp /= "Data.bin";
	mDataHeaderTemp = mTempDirectory;
	mDataHeaderTemp /= "Data.h";
	mTexturesFileTemp = mTempDirectory;
	mTexturesFileTemp /= "Textures.bin";

	// Output file and directory
	if (!std::filesystem::exists(outputDirectory))
	{
		MessageBox(nullptr, outputDirectory.string().c_str(), "Output directory will be created", MB_OK | MB_SYSTEMMODAL);
		std::filesystem::create_directories(outputDirectory);
	}

#if 0 // Clean
	std::filesystem::remove(mDataFile);
	std::filesystem::remove(mTexturesFile);
#endif

	if (std::filesystem::exists(mDataFile))
	{
		mDataFileLastWriteTime = std::filesystem::last_write_time(mDataFile);
		auto [pcDate, pcTime] = common::FileTimeString(mDataFileLastWriteTime);
		LOG("Data file \"{}\" last modified time: {} {}", mDataFile.string(), pcDate, pcTime);
	}
	else
	{
		LOG("Data file does not exist: \"{}\"", mDataFile.string());
		mbCleanExport = true;
	}

	if (std::filesystem::exists(mTexturesFile))
	{
		mTexturesFileLastWriteTime = std::filesystem::last_write_time(mTexturesFile);
		auto [pcDate, pcTime] = common::FileTimeString(mTexturesFileLastWriteTime);
		LOG("Textures file \"{}\" last modified time: {} {}", mTexturesFile.string(), pcDate, pcTime);
	}
	else
	{
		LOG("Textures file does not exist: \"{}\"", mTexturesFile.string());
		mbCleanExport = true;
	}

	if (mbCleanExport)
	{
		std::filesystem::remove(mDataFile);
		std::filesystem::remove(mTexturesFile);
	}
}

FileManager::~FileManager()
{
	gpFileManager = nullptr;
}
