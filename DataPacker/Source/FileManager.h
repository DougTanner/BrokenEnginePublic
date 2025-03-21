#pragma once

class FileManager
{
public:

	FileManager(std::span<char*> argvSpan);
	~FileManager();

	std::filesystem::path mWindowsSdkBinariesDirectory;
	std::filesystem::path mVulkanSdkBinariesDirectory;

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mTempDirectory;
	std::filesystem::path mOutputDirectory;

	bool mbCleanExport = false;
};

inline FileManager* gpFileManager = nullptr;
