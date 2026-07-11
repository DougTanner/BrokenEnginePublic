#pragma once

#include "InputFingerprint.h"

class FileManager
{
public:

	FileManager(std::span<char*> argvSpan);
	~FileManager();

	std::filesystem::path mpInputDirectories[2];
	std::filesystem::path mTempDirectory;
	std::filesystem::path mGaeaCacheDirectory;
	std::filesystem::path mOutputDirectory;
	std::filesystem::path mThirdPartyDirectory;
	std::string mProjectName;

	bool mbCleanExport = false;

	std::string GetFingerprint(const std::filesystem::path& rPath);

private:

	std::unique_ptr<InputFingerprintCache> mpInputFingerprintCache;
};

inline FileManager* gpFileManager = nullptr;
