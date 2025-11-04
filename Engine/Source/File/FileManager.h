#pragma once

namespace engine
{

#if defined(BT_DEBUG)
inline constexpr char kpcLogFile[] = "Debug.txt";
#elif defined(BT_PROFILE)
inline constexpr char kpcLogFile[] = "Profile.txt";
#elif defined(BT_RELEASE)
inline constexpr char kpcLogFile[] = "Release.txt";
#endif

enum class FileFlags : uint64_t
{
	kAppDataDirectory = 0x01,
	kTempDirectory    = 0x02,

	kRead   = 0x08,
	kWrite  = 0x10,
	kBackup = 0x20,
};
using FileFlags_t = common::Flags<FileFlags>;

// Eager chunk (loaded at boot)
struct EagerChunk
{
	common::ChunkHeader* pHeader = nullptr;
	byte* pData = nullptr;
};

// Lazy chunk (loaded on demand)
enum class LoadState : uint32_t
{
	kNotLoaded = 0,
	kLoading = 1,
	kLoaded = 2,
};

struct LazyChunk
{
	data::DataTypes eDataType = data::kDataTypeCount; // Which pack file it is in
	common::ChunkLocation location;                   // Offset and size in pack file, maps manifest file
	bool bLoadRequested = false;                      // Set to true when a load is requested
	bool bLoaded = false;                             // Set to true when data is loaded
	common::ChunkHeader header {};                    // Chunk header

	std::vector<byte> data;                           // Actual data (empty until loaded)
};

// Load request for background thread
enum class LoadPriority : uint32_t
{
	kLow = 0,
	kNormal = 1,
	kHigh = 2,
	kRealtime = 3,
};

struct LoadRequest
{
	common::crc_t crc;
	LoadPriority priority;
	
	// Priority queue needs comparison operator
	bool operator<(const LoadRequest& other) const
	{
		return priority < other.priority;
	}
};

class FileManager
{
public:

	FileManager();
	~FileManager();

	bool Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	int64_t GetFileSize(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	std::fstream OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	void RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	std::filesystem::path LogFile()
	{
		std::filesystem::path logFile(mAppDataDirectory);
		logFile /= std::string("Log.") + kpcLogFile;
		return logFile;
	}

	void WriteLogFile(std::ofstream& rOfstream)
	{
		std::ifstream logFileStream(LogFile(), std::ofstream::in);
		rOfstream << logFileStream.rdbuf() << std::flush;
	}

	const std::unordered_map<common::crc_t, EagerChunk>& GetEagerChunkMap() const;
	const std::unordered_map<common::crc_t, LazyChunk>& GetLazyChunkMap() const;
	
	// Lazy loading APIs
	bool IsChunkReady(common::crc_t crc) const;
	void RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority priority = LoadPriority::kNormal);
	void WaitForChunks(std::span<const common::crc_t> crcs);
	
	// Streaming API for reading data at specific offset within a chunk
	bool ReadChunkData(common::crc_t crc, uint64_t offset, std::span<byte> buffer);

	std::vector<common::ChunkLocation> mpChunkLocations[data::kDataTypeCount];
	std::future<void> mLoadingFuture;

private:

	std::filesystem::path GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	void LoadPackFiles();
	void LoadingThread();
	void LoadChunk(const LoadRequest& rRequest);

	std::filesystem::path mAppDataDirectory;
	std::filesystem::path mTempDirectory;
	std::filesystem::path mDataDirectory;

	// Only available for eager pack files
	std::vector<byte> mPackFileData[data::kDataTypeCount];
	
	// Split chunk maps for eager and lazy loading
	std::unordered_map<common::crc_t, EagerChunk> mEagerChunkMap;  // Font, Gltf, Model, Shaders
	std::unordered_map<common::crc_t, LazyChunk> mLazyChunkMap;  // Audio, Islands, Texture
	
	// Background loading thread
	std::thread mLoadingThread;
	std::condition_variable mWakeCondition;
	std::condition_variable mCompletionCondition;
	mutable std::mutex mQueueMutex;
	std::priority_queue<LoadRequest> mRequestQueue;
	std::atomic<bool> mShutdown{false};

	std::ofstream mLogFileStream;
};

inline FileManager* gpFileManager = nullptr;

template <typename STRUCT_TYPE>
bool ExistsVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	if (!gpFileManager->Exists(rFlags, rFilename))
	{
		return false;
	}

	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = 0;
	fileStream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = 0;
	fileStream.read(reinterpret_cast<char*>(&iSize), sizeof(iSize));

	return iVersion == STRUCT_TYPE::kiVersion && iSize == sizeof(STRUCT_TYPE);
}

template <typename STRUCT_TYPE>
void WriteVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = STRUCT_TYPE::kiVersion;
	fileStream.write(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = sizeof(STRUCT_TYPE);
	fileStream.write(reinterpret_cast<char*>(&iSize), sizeof(iSize));
	LOG("Write iSize: {}", iSize);
	fileStream.write(reinterpret_cast<char*>(&rStructure), sizeof(rStructure));
}

template <typename STRUCT_TYPE>
bool ReadVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	int64_t iVersion = 0;
	fileStream.read(reinterpret_cast<char*>(&iVersion), sizeof(iVersion));
	int64_t iSize = 0;
	fileStream.read(reinterpret_cast<char*>(&iSize), sizeof(iSize));
	LOG("    iVersion: {} == {} iSize: {} == {}", iVersion, STRUCT_TYPE::kiVersion, iSize, sizeof(STRUCT_TYPE));
	if (iVersion == STRUCT_TYPE::kiVersion && iSize == sizeof(STRUCT_TYPE))
	{
		int64_t iBytesRead = fileStream.read(reinterpret_cast<char*>(&rStructure), sizeof(rStructure)).gcount();
		int64_t iExpectedBytes = sizeof(STRUCT_TYPE);
		return iBytesRead == iExpectedBytes;
	}

	LOG("    Failed to load versioned file");

	if (iVersion == STRUCT_TYPE::kiVersion && iSize != sizeof(STRUCT_TYPE))
	{
		// If this is hit, Frame::kiVersion might be missing a sub-version
		DEBUG_BREAK();
	}

	return false;
}

} // namespace engine
