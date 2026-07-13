#pragma once

class InputFingerprintCache
{
public:

	InputFingerprintCache(const std::filesystem::path& rRepositoryRoot);

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

	struct GitIndexEntry
	{
		std::string blobId;
		uintmax_t uiSize = 0;
		int64_t iChangeTimeSeconds = 0;
		int64_t iChangeTimeNanoseconds = 0;
		int64_t iLastWriteTimeSeconds = 0;
		int64_t iLastWriteTimeNanoseconds = 0;
	};

	std::string GetUnlocked(const std::filesystem::path& rPath);
	std::string GetFile(const std::filesystem::path& rPath);
	std::string GetPersistentFile(const std::filesystem::path& rPath);
	std::string GetDirectory(const std::filesystem::path& rPath);
	void LoadGitIndex(const std::filesystem::path& rRepositoryRoot);

	static FileSnapshot Snapshot(const std::filesystem::path& rPath);
	static bool MatchesGitIndex(const FileSnapshot& rSnapshot, const GitIndexEntry& rGitEntry);
	static std::string PathKey(const std::filesystem::path& rPath);

	std::mutex mMutex;
	std::unordered_map<std::string, CachedFingerprint> mCachedFingerprints;
	std::unordered_map<std::string, GitIndexEntry> mGitIndex;
};
