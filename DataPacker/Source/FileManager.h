#pragma once

class FileManager
{
public:

	FileManager(std::span<char*> argvSpan);
	~FileManager();

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mTempDirectory;
	std::filesystem::path mOutputDirectory;
	std::string mProjectName;

	bool mbCleanExport = false;
};

inline FileManager* gpFileManager = nullptr;
