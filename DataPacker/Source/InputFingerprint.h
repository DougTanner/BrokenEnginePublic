#pragma once

class InputFingerprintCache
{
public:

	InputFingerprintCache(const std::filesystem::path& rCacheFile);
	~InputFingerprintCache();

	std::string Get(const std::filesystem::path& rPath);
	std::string GetPersistent(const std::filesystem::path& rPath);

private:

	struct FileSnapshot
	{
		uintmax_t uiSize = 0;
		int64_t iLastWriteTime = 0;
		int64_t iCreationTime = 0;
		int64_t iChangeTime = 0;
		uint64_t uiFileId = 0;
		uint32_t uiVolumeSerialNumber = 0;

		bool operator==(const FileSnapshot&) const = default;
	};

	struct CachedFingerprint
	{
		FileSnapshot snapshot;
		std::string fingerprint;
	};

	std::string GetUnlocked(const std::filesystem::path& rPath);
	std::string GetFile(const std::filesystem::path& rPath);
	std::string GetPersistentFile(const std::filesystem::path& rPath);
	std::string GetDirectory(const std::filesystem::path& rPath);
	void Load();
	void Save();

	static FileSnapshot Snapshot(const std::filesystem::path& rPath);
	static std::string PathKey(const std::filesystem::path& rPath);

	std::filesystem::path mCacheFile;
	bool mbDirty = false;
	std::mutex mMutex;
	std::unordered_map<std::string, CachedFingerprint> mCachedFingerprints;
};
