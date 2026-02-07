#pragma once

#include "Data/DataTypes.h"

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
enum class ChunkState : uint32_t
{
	kNotLoaded = 0,
	kLoadRequested = 1,
	kDiskLoaded = 2,
	kUploading = 3,
	kGpuUploadComplete = 4,
	kReady = 5,
};

// Movable atomic wrapper (std::atomic deletes copy/move, breaking aggregate types in containers)
struct MovableAtomicChunkState
{
	std::atomic<ChunkState> value {ChunkState::kNotLoaded};

	MovableAtomicChunkState() = default;
	MovableAtomicChunkState(const MovableAtomicChunkState& rOther) : value(rOther.value.load(std::memory_order_relaxed)) {}
	MovableAtomicChunkState(MovableAtomicChunkState&& rOther) noexcept : value(rOther.value.load(std::memory_order_relaxed)) {}
	MovableAtomicChunkState& operator=(const MovableAtomicChunkState&) = delete;
	MovableAtomicChunkState& operator=(MovableAtomicChunkState&&) = delete;

	void store(ChunkState eVal, std::memory_order order = std::memory_order_seq_cst) { value.store(eVal, order); }
	ChunkState load(std::memory_order order = std::memory_order_seq_cst) const { return value.load(order); }
};

struct LazyChunk
{
	data::DataTypes eDataType = data::kDataTypeCount; // Which pack file it is in
	common::ChunkLocation location;                   // Offset and size in pack file, maps manifest file
	MovableAtomicChunkState eState {};                // Atomic state tracking load progress
	common::ChunkHeader header {};                    // Chunk header

	std::vector<byte> data;                           // Actual data (empty until loaded)

	// GPU upload results (written by upload thread, read by main thread)
	VkImage vkImage = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
	VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;
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

struct MemoryStats
{
	int64_t iBytes = 0;
	int64_t iCount = 0;
};

constexpr bool IsEagerChunk(data::DataTypes eDataType);

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

	// Notification for chunk completion (wakes WaitForChunks waiters)
	void NotifyChunkCompletion();
	LazyChunk& GetLazyChunk(common::crc_t crc);

	// Memory profiling
	int64_t GetEagerMemoryBytes() const;
	int64_t GetLazyMemoryBytes() const;
	int64_t GetEagerAllocationCount() const;
	int64_t GetLazyAllocationCount() const;
	MemoryStats GetMemoryStats(data::DataTypes eDataType) const;

	std::vector<common::ChunkLocation> mpChunkLocations[data::kDataTypeCount];
	std::future<void> mLoadingFuture;

private:

	std::filesystem::path GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	void LoadPackFiles();
	void LoadingThread();
	void LoadChunk(const LoadRequest& rRequest);
	std::filesystem::path GetDataFilePath(data::DataTypes eDataType, std::string_view extension) const;

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
	std::atomic<bool> mShutdown {false};

	std::ofstream mLogFileStream;
};

inline FileManager* gpFileManager = nullptr;

// Type trait to detect if a type has both operator<< and operator>> for binary stream serialization
// Excludes built-in arithmetic types, pointers, and std::string to avoid false positives from text formatters
template<typename T, typename = void>
struct has_binary_stream_operators : std::false_type {};

template<typename T>
struct has_binary_stream_operators
<T,
	std::enable_if_t
	<
		!std::is_arithmetic_v<T> &&
		!std::is_pointer_v<T> &&
		!std::is_same_v<std::decay_t<T>, std::string> &&
		!std::is_same_v<std::decay_t<T>, std::string_view>,
		std::void_t
		<
			decltype(std::declval<std::ostream&>() << std::declval<const T&>()),
			decltype(std::declval<std::istream&>() >> std::declval<T&>())
		>
	>
> : std::true_type {};

template<typename T>
inline constexpr bool has_binary_stream_operators_v = has_binary_stream_operators<T>::value;

template <typename STRUCT_TYPE>
bool ExistsVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename)
{
	if (!gpFileManager->Exists(rFlags, rFilename))
	{
		return false;
	}

	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = 0;
	common::Read(fileStream, iVersion);
	int64_t iSize = 0;
	common::Read(fileStream, iSize);

	if constexpr (std::is_trivially_copyable_v<STRUCT_TYPE>)
	{
		return iVersion == STRUCT_TYPE::kiVersion && iSize == sizeof(STRUCT_TYPE);
	}
	else
	{
		return iVersion == STRUCT_TYPE::kiVersion;
	}
}

template <typename STRUCT_TYPE>
void WriteVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);
	int64_t iVersion = STRUCT_TYPE::kiVersion;
	common::Write(fileStream, iVersion);
	int64_t iSize = std::is_trivially_copyable_v<STRUCT_TYPE> ? sizeof(STRUCT_TYPE) : 0;
	common::Write(fileStream, iSize);
	Log("WriteVersionedFile {} iVersion: {} iSize: {}", rFilename, iVersion, iSize);

	if constexpr (has_binary_stream_operators_v<STRUCT_TYPE>)
	{
		fileStream << rStructure;
	}
	else
	{
		common::Write(fileStream, rStructure);
	}
}

template <typename STRUCT_TYPE>
bool ReadVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	Log("ReadVersionedFile {} iVersion: {} iSize: {}", rFilename, STRUCT_TYPE::kiVersion, sizeof(STRUCT_TYPE));
	int64_t iVersion = 0;
	common::Read(fileStream, iVersion);
	int64_t iSize = 0;
	common::Read(fileStream, iSize);
	Log("    iVersion: {} == {} iSize: {} == {}", iVersion, STRUCT_TYPE::kiVersion, iSize, sizeof(STRUCT_TYPE));
	bool bSizeValid = std::is_trivially_copyable_v<STRUCT_TYPE> ? (iSize == sizeof(STRUCT_TYPE)) : true;
	if (iVersion == STRUCT_TYPE::kiVersion && bSizeValid)
	{
		if constexpr (has_binary_stream_operators_v<STRUCT_TYPE>)
		{
			fileStream >> rStructure;
			return fileStream.good();
		}
		else
		{
			common::Read(fileStream, rStructure);
			int64_t iBytesRead = fileStream.gcount();
			int64_t iExpectedBytes = sizeof(STRUCT_TYPE);
			return iBytesRead == iExpectedBytes;
		}
	}

	Log("    Failed to load versioned file");

	if constexpr (std::is_trivially_copyable_v<STRUCT_TYPE>)
	{
		if (iVersion == STRUCT_TYPE::kiVersion && iSize != sizeof(STRUCT_TYPE))
		{
			// If this is hit, Frame::kiVersion might be missing a sub-version
			common::DebugBreak();
		}
	}

	return false;
}

} // namespace engine
