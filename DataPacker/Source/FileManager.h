#pragma once

class FileManager
{
public:

	FileManager(std::span<char*> argvSpan);
	~FileManager();

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mTempDirectory;
	std::filesystem::path mWindowsSdkBinariesDirectory;
	std::filesystem::path mVulkanSdkBinariesDirectory;

	std::filesystem::path mDataHeader;
	std::filesystem::path mDataHeaderTemp;

	std::filesystem::path mDataFile;
	std::filesystem::path mDataFileTemp;
	std::filesystem::file_time_type mDataFileLastWriteTime;

	std::filesystem::path mTexturesFile;
	std::filesystem::path mTexturesFileTemp;
	std::filesystem::file_time_type mTexturesFileLastWriteTime;

	bool mbCleanExport = false;
};

inline FileManager* gpFileManager = nullptr;
