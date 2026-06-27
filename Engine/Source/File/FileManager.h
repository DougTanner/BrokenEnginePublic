#pragma once

#include "Data/DataTypes.h"

namespace engine
{

enum class FileFlags : uint64_t
{
	kAppDataDirectory = 0x01,
	kTempDirectory    = 0x02,

	kRead      = 0x08,
	kWrite     = 0x10,
	kBackup    = 0x20,
	kStreaming = 0x40, // Required when calling OpenFile with kWrite. Opt-out of atomic write; one-shots use WriteFileAtomically.
};
using FileFlags_t = common::Flags<FileFlags>;

// Eager chunk (loaded at boot)
struct EagerChunk
{
	common::ChunkHeader* pHeader = nullptr;
	std::byte* pData = nullptr;
	int64_t iDataSize = 0; // True in-memory data extent (ChunkLocation::uiSize - kiChunkDataOffset); unlike pHeader->iSize this includes a scene chunk's appended animation section
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
	common::ChunkLocation location;                   // Offset and size in pack file, maps manifest file
	MovableAtomicChunkState eState {};                // Atomic state tracking load progress
	common::ChunkHeader header {};                    // Chunk header

	std::byte* pData = nullptr;                       // Points into FileManager's pre-allocated pool (null until assigned)
	int64_t iDataSize = 0;

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
	LoadPriority ePriority;

	// Priority queue needs comparison operator
	bool operator<(const LoadRequest& rOther) const
	{
		return ePriority < rOther.ePriority;
	}
};

struct MemoryStats
{
	int64_t iBytes = 0;
	int64_t iCount = 0;
};

constexpr bool IsEagerChunk(data::DataTypes eDataType);
// Server-only predicate: which lazy data types the headless server actually consumes.
// Used to skip opening (and locking) pack files the server never reads — Audio, Texture, etc.
constexpr bool IsServerChunk(data::DataTypes eDataType);

class FileManager
{
public:

	FileManager();
	~FileManager();

	FileManager(const FileManager&) = delete; // Owns a raw std::thread whose lambda captures `this`; deleting copy also suppresses the implicit move
	FileManager& operator=(const FileManager&) = delete;

	bool Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	std::fstream OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	void RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	// Crash-safe write: opens "<rFilename>.tmp" for write, runs fnWrite(stream), closes, then atomically renames to rFilename.
	// On stream failure or rename failure the previous good file remains intact and the .tmp is removed. Returns false on any failure.
	template <typename FN>
	[[nodiscard]] bool WriteFileAtomically(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, FN&& fnWrite);

	const std::unordered_map<common::crc_t, EagerChunk>& GetEagerChunkMap() const;
	const std::unordered_map<common::crc_t, LazyChunk>& GetLazyChunkMap() const;
	
	// Lazy loading APIs
	bool IsChunkReady(common::crc_t crc) const;
	void RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority ePriority = LoadPriority::kNormal);
	void WaitForChunks(std::span<const common::crc_t> crcs);
	
	// Streaming API for reading data at specific offset within a chunk
	bool ReadChunkData(common::crc_t crc, uint64_t uiOffset, std::span<std::byte> buffer);

	// Notification for chunk completion (wakes WaitForChunks waiters)
	void NotifyChunkCompletion();
	LazyChunk& GetLazyChunk(common::crc_t crc);

	void ResetTextureChunkStates();
	void ResetTextureChunkStates(std::span<const common::crc_t> targetCrcs);

	// Memory profiling
	MemoryStats GetEagerStats() const;
	MemoryStats GetLazyStats() const;
	MemoryStats GetMemoryStats(data::DataTypes eDataType) const;

private:

	std::filesystem::path GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	bool CommitAtomicWrite(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, bool bWriteSucceeded);
	void BackupExistingFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	void LoadPackFiles();
	void LoadingThread();
	void LoadChunk(const LoadRequest& rRequest);
	std::filesystem::path GetDataFilePath(data::DataTypes eDataType, std::string_view extension) const;

	std::filesystem::path mAppDataDirectory;
	std::filesystem::path mTempDirectory;
	std::filesystem::path mDataDirectory;

	// Cached pack file paths (initialized once in LoadPackFiles)
	std::filesystem::path mPackFilePaths[data::kDataTypeCount];

	// Only available for eager pack files
	std::vector<std::byte> mPackFileData[data::kDataTypeCount];

	// Per-data-type chunk-location tables (offset/size/crc), read from each manifest in LoadPackFiles
	std::vector<common::ChunkLocation> mpChunkLocations[data::kDataTypeCount];

	// Split chunk maps for eager and lazy loading
	std::unordered_map<common::crc_t, EagerChunk> mEagerChunkMap;  // Font, Model, Shaders
	std::unordered_map<common::crc_t, LazyChunk> mLazyChunkMap;  // Audio, Islands, Texture
	
	// Background loading thread, assigned inside the async eager-load task (mLoadingFuture), not the ctor body. Its LoadingThread reads the sync members below (mWakeCondition/mQueueMutex/mRequestQueue/mShutdown), so ~FileManager first drains mLoadingFuture (ensuring this assignment has happened), then sets mShutdown + notifies + join()s the thread before those members destruct.
	std::thread mLoadingThread;
	std::condition_variable mWakeCondition;
	std::condition_variable mCompletionCondition;
	mutable std::mutex mQueueMutex;
	std::priority_queue<LoadRequest> mRequestQueue;
	std::atomic<bool> mShutdown {false};

	// Eager-load completion, assigned in LoadPackFiles. mutable: the first GetEagerChunkMap() drains it
	// (a lazy completion behind the const accessor).
	mutable std::future<void> mLoadingFuture;

	// Published (release) at the end of the async eager-load task; eager-map readers (ReadChunkData,
	// IsChunkReady, the memory-stats getters) acquire it before touching mEagerChunkMap / mPackFileData,
	// which the task populates. Gates the boot window only — always true once the first frame runs.
	std::atomic<bool> mbEagerLoadComplete {false};

	// Persistent pack file handles for lazy loading (opened with FILE_FLAG_NO_BUFFERING)
	HANDLE mLazyPackFileHandles[data::kDataTypeCount] {};

	// Sector-aligned read buffer (reused across all chunk reads)
	std::byte* mpReadBuffer = nullptr;
	int64_t miReadBufferSize = 0;
	int64_t miSectorSize = 0;

	// Pre-allocated memory pool for all lazy chunk data (VirtualAlloc MEM_COMMIT — committed, not pre-faulted)
	std::byte* mpLazyPool = nullptr;
	int64_t miLazyPoolSize = 0;

	// Scratch buffer used by the loading thread for zlib-compressed chunks.
	// Sized at boot to the largest compressed chunk on disk; reused per chunk.
	std::byte* mpDecompressScratch = nullptr;
	int64_t miDecompressScratchSize = 0;

	// Sub-read size for chunked disk reads (256KB balances NVMe throughput vs L3 cache pressure)
	static constexpr int64_t kiSubReadSize = 256 * 1024;
};

inline FileManager* gpFileManager = nullptr;

// Type trait to detect if a type has both operator<< and operator>> for binary stream serialization
// Excludes built-in arithmetic types, pointers, and std::string to avoid false positives from text formatters
template <typename T, typename = void>
struct has_binary_stream_operators : std::false_type {};

template <typename T>
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

template <typename T>
inline constexpr bool has_binary_stream_operators_v = has_binary_stream_operators<T>::value;

template <typename FN>
bool FileManager::WriteFileAtomically(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, FN&& fnWrite)
{
	if (rFlags & FileFlags::kBackup)
	{
		BackupExistingFile(rFlags, rFilename);
	}

	// OpenFile is called on the .tmp filename, which doesn't exist yet, so kBackup must be stripped to avoid a no-op second backup.
	// kStreaming is added because WriteFileAtomically is the only legitimate kWrite-without-kStreaming caller.
	FileFlags_t openFlags = rFlags;
	openFlags.Clear(FileFlags::kBackup);
	openFlags.Set(FileFlags::kStreaming);

	std::filesystem::path tmpFilename = rFilename;
	tmpFilename += ".tmp";

	std::fstream stream = OpenFile(openFlags, tmpFilename);
	if (!stream.is_open())
	{
		LOG(kLoading, kError, "WriteFileAtomically failed to open \"{}.tmp\"", rFilename.string());
		return false;
	}

	fnWrite(stream);
	bool bGood = stream.good();
	stream.close();

	return CommitAtomicWrite(rFlags, rFilename, bGood);
}

// Shared version+size on-disk header convention. Writes int64 version then int64 size (sizeof for
// trivially-copyable types, 0 otherwise — non-trivial types validate version only). Single source for
// WriteVersionedFile/ReadVersionedFile, DifferenceStream save/load, and GameSaveLoad grid saves.
template <typename STRUCT_TYPE>
void WriteVersionHeader(std::fstream& rFileStream)
{
	common::Write(rFileStream, static_cast<int64_t>(STRUCT_TYPE::kiVersion));
	common::Write(rFileStream, std::is_trivially_copyable_v<STRUCT_TYPE> ? static_cast<int64_t>(sizeof(STRUCT_TYPE)) : int64_t{0});
}

// Reads the version+size header into the out-params and applies the validity rule. Out-params are
// load-bearing: callers print the read values on mismatch and re-test for the size-mismatch DEBUG_BREAK.
template <typename STRUCT_TYPE>
bool ReadAndValidateVersionHeader(std::fstream& rFileStream, int64_t& riVersion, int64_t& riSize)
{
	common::Read(rFileStream, riVersion);
	common::Read(rFileStream, riSize);
	bool bSizeValid = std::is_trivially_copyable_v<STRUCT_TYPE> ? (riSize == static_cast<int64_t>(sizeof(STRUCT_TYPE))) : true;
	return riVersion == STRUCT_TYPE::kiVersion && bSizeValid;
}

template <typename STRUCT_TYPE>
bool WriteVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	return gpFileManager->WriteFileAtomically(rFlags, rFilename, [&](std::fstream& rFileStream)
	{
		int64_t iVersion = STRUCT_TYPE::kiVersion;
		int64_t iSize = std::is_trivially_copyable_v<STRUCT_TYPE> ? sizeof(STRUCT_TYPE) : 0;
		WriteVersionHeader<STRUCT_TYPE>(rFileStream);
		LOG(kLoading, kDebug, "WriteVersionedFile {} iVersion: {} iSize: {}", rFilename, iVersion, iSize);

		if constexpr (has_binary_stream_operators_v<STRUCT_TYPE>)
		{
			rFileStream << rStructure;
		}
		else
		{
			common::Write(rFileStream, rStructure);
		}
	});
}

template <typename STRUCT_TYPE>
bool ReadVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, STRUCT_TYPE& rStructure)
{
	std::fstream fileStream = gpFileManager->OpenFile(rFlags, rFilename);

	LOG(kLoading, kDebug, "ReadVersionedFile {} iVersion: {} iSize: {}", rFilename, STRUCT_TYPE::kiVersion, sizeof(STRUCT_TYPE));
	int64_t iVersion = 0;
	int64_t iSize = 0;
	bool bHeaderValid = ReadAndValidateVersionHeader<STRUCT_TYPE>(fileStream, iVersion, iSize);
	LOG(kLoading, kDebug, "    iVersion: {} == {} iSize: {} == {}", iVersion, STRUCT_TYPE::kiVersion, iSize, sizeof(STRUCT_TYPE));
	if (bHeaderValid)
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

	LOG(kLoading, kDebug, "    Failed to load versioned file");

	if constexpr (std::is_trivially_copyable_v<STRUCT_TYPE>)
	{
		if (iVersion == STRUCT_TYPE::kiVersion && iSize != sizeof(STRUCT_TYPE))
		{
			// If this is hit, Frame::kiVersion might be missing a sub-version
			DEBUG_BREAK();
		}
	}

	return false;
}

} // namespace engine
