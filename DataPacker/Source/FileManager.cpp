#include "FileManager.h"

FileManager::FileManager(std::span<char*> argvSpan)
{
	gpFileManager = this;

	if (argvSpan.size() == 1)
	{
		mpInputDirectories[0] = "../../../Engine/Data";
		mpInputDirectories[1] = "../../../Projects/BrokenEngineSandbox/Data";
		mOutputDirectory = "../../../Projects/BrokenEngineSandbox/Platforms/VisualStudio2022/Output/Data";
	}
	else
	{
		ASSERT(argvSpan.size() == 4);
		mpInputDirectories[0] = argvSpan[1];
		mpInputDirectories[1] = argvSpan[2];
		mOutputDirectory = argvSpan[3];
	}

	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[0]));
	VERIFY_SUCCESS(std::filesystem::exists(mpInputDirectories[1]));
	std::filesystem::create_directories(mOutputDirectory);
	VERIFY_SUCCESS(std::filesystem::exists(mOutputDirectory));

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

		std::vector<std::string> split = common::Split(rDirectoryEntry.path().stem().string(), std::string("."));
		if (split.size() != 3)
		{
			// Wrong directory format
			continue;
		}

		// DT: TODO adpcmencode3.exe in later SDKs is broken, find alternative encoding method/format
		if (rDirectoryEntry.path().string().find("10.0.19041.0") != std::string::npos)
		{
			iHighestVersion = 19041;
			mWindowsSdkBinariesDirectory = rDirectoryEntry.path();
			break;
		}

		int64_t iVersion = 0;
		try
		{
			iVersion = std::stoi(split.back());
		}
		catch (...)
		{
			// Skip directories that don't have valid version numbers
			continue;
		}
		if (iVersion > iHighestVersion)
		{
			std::filesystem::path adpcmencode3File(rDirectoryEntry.path());
			adpcmencode3File.append("x64\\\\adpcmencode3.exe");
			if (std::filesystem::exists(adpcmencode3File))
			{
				iHighestVersion = iVersion;
				mWindowsSdkBinariesDirectory = rDirectoryEntry.path();
			}
		}
	}

	if (mWindowsSdkBinariesDirectory.empty())
	{
		throw std::runtime_error("Windows SDK binaries not found (typically in C:\\Program Files (x86)\\Windows Kits\\10\\bin\\)");
	}

	mWindowsSdkBinariesDirectory.append("x64");
	LOG("    Found: \"{}\"", gpFileManager->mWindowsSdkBinariesDirectory.string());

	// Vulkan SDK Path
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

	// Temporaries directory
	DWORD tempResult = GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	if (tempResult == 0)
	{
		throw std::runtime_error("Failed to get temp directory path");
	}
	mTempDirectory = pcDirectory;
	VERIFY_SUCCESS(std::filesystem::exists(mTempDirectory));
	mTempDirectory.append("DataPacker");
	std::filesystem::create_directories(mTempDirectory);
	LOG("Temp directory: \"{}\"", gpFileManager->mTempDirectory.string());

	// Output file and directory
	if (!std::filesystem::exists(mOutputDirectory))
	{
		MessageBox(nullptr, mOutputDirectory.string().c_str(), "Output directory will be created", MB_OK | MB_SYSTEMMODAL);
		std::filesystem::create_directories(mOutputDirectory);
	}
}

FileManager::~FileManager()
{
	gpFileManager = nullptr;
}
