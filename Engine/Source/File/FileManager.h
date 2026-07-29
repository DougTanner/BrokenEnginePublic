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

struct FileContentDigest
{
	int64_t iByteCount = 0;
	std::array<uint8_t, 32> sha256 {};
};

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

// Completion state for one asynchronously reloaded sub-range of a lazy chunk. The range metadata and state are
// owned by LazyChunk so a completion never depends on a caller-owned object surviving the loading thread.
enum class ChunkRangeReloadState : uint32_t
{
	kIdle = 0,
	kPending = 1,
	kReady = 2,
	kFailed = 3,
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

// Movable atomic wrapper for a lazy chunk's asynchronous range-reload completion state.
struct MovableAtomicChunkRangeReloadState
{
	std::atomic<ChunkRangeReloadState> value {ChunkRangeReloadState::kIdle};

	MovableAtomicChunkRangeReloadState() = default;
	MovableAtomicChunkRangeReloadState(const MovableAtomicChunkRangeReloadState& rOther)
		: value(rOther.value.load(std::memory_order_relaxed))
	{
	}
	MovableAtomicChunkRangeReloadState(MovableAtomicChunkRangeReloadState&& rOther) noexcept
		: value(rOther.value.load(std::memory_order_relaxed))
	{
	}
	MovableAtomicChunkRangeReloadState& operator=(const MovableAtomicChunkRangeReloadState&) = delete;
	MovableAtomicChunkRangeReloadState& operator=(MovableAtomicChunkRangeReloadState&&) = delete;

	void store(ChunkRangeReloadState eVal, std::memory_order order = std::memory_order_seq_cst)
	{
		value.store(eVal, order);
	}
	ChunkRangeReloadState load(std::memory_order order = std::memory_order_seq_cst) const
	{
		return value.load(order);
	}
};

struct LazyChunk
{
	common::ChunkLocation location;                   // Manifest entry for pack offset, size, path CRC, and content CRC
	MovableAtomicChunkState eState {};                // Atomic state tracking load progress
	common::ChunkHeader header {};                    // Chunk header

	std::byte* pData = nullptr;                       // Points into the pre-allocated lazy pool (null until assigned)
	int64_t iDataSize = 0;

	// One asynchronous recommit/reload range. Its offset and length are written before the pending release-store
	// and remain stable until the consumer resets a ready or failed terminal state.
	uint64_t uiRangeReloadOffset = 0;
	uint64_t uiRangeReloadLength = 0;
	MovableAtomicChunkRangeReloadState eRangeReloadState {};

	// GPU upload results (written by upload thread, read by main thread)
	VkImage vkImage = VK_NULL_HANDLE;
	VmaAllocation vmaAllocation = VK_NULL_HANDLE;
};

// Load request for background thread
enum class LoadPriority : uint32_t
{
	kLow = 0,
	kNormal = 1,
	kHigh = 2,
	kRealtime = 3,
};

enum class LoadRequestKind : uint32_t
{
	kWholeChunk,
	kRangeReload,
};

struct LoadRequest
{
	common::crc_t crc;
	LoadPriority ePriority;
	LoadRequestKind eKind = LoadRequestKind::kWholeChunk;
	uint64_t uiOffset = 0;
	uint64_t uiLength = 0;

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

// Owned by FileManager (std::unique_ptr, forward-declared for the compile firewall): the packed-asset chunk
// engine (eager buffers, lazy maps, loading threads, VirtualAlloc pool). Definition in PackChunks.h, included
// only by PackChunks.cpp + FileManager.cpp.
class PackChunks;

class FileManager
{
public:

	FileManager();
	~FileManager();

	FileManager(const FileManager&) = delete; // Holds a std::unique_ptr<PackChunks> (non-copyable); deleting copy also suppresses the implicit move
	FileManager& operator=(const FileManager&) = delete;

	bool Exists(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	std::fstream OpenFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	void RemoveFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	[[nodiscard]] bool ComputeSha256(std::span<const std::byte> bytes, std::array<uint8_t, 32>& rOut);
	[[nodiscard]] bool ComputeOrdinaryFileSha256(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, FileContentDigest& rOut);

	// Crash-safe write: opens "<rFilename>.tmp" for write, runs fnWrite(stream), closes, then atomically renames to rFilename.
	// On stream failure or rename failure the previous good file remains intact and the .tmp is removed. Returns false on any failure.
	template <typename FN>
	[[nodiscard]] bool WriteFileAtomically(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, FN&& fnWrite);

	const std::unordered_map<common::crc_t, EagerChunk>& GetEagerChunkMap() const;
	const std::unordered_map<common::crc_t, LazyChunk>& GetLazyChunkMap() const;
	common::crc_t GetPackIntegrityToken() const;
	
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

	// Reclaim a dead sub-range of a resident lazy chunk's decompressed pool memory. Decommits only the
	// page-aligned interior of [uiOffset, uiOffset + uiLength); the boundary partial-pages (which may share
	// bytes with the neighbouring payload) and every other chunk stay committed, and the chunk's pData pointer
	// is unchanged. A consumer must recommit and reload the range before reading it again. Main-thread
	// only (boot / device-loss recovery / transfer-complete texture adoption) — the range must have no concurrent reader.
	void DecommitChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);
	// Inverse of DecommitChunkRange: MEM_COMMITs the interior and re-reads [uiOffset, uiOffset + uiLength)
	// straight from the pack file on disk into the pool (NOT via the decommitted resident copy). Uncompressed chunks only.
	// Returns true on success; false on soft-fail (MEM_COMMIT failure / pack-open failure / short read). On false the
	// caller must NOT read the range — the interior may be decommitted or hold partial data.
	[[nodiscard]] bool RecommitAndReloadChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);
	// Queues a single uncompressed lazy-chunk range for background recommit/reload. Same-range requests deduplicate
	// while pending or ready; a failed request stays failed until its consumer resets it. State reads acquire the
	// worker's ready/failed publication, and reset refuses a pending request so it cannot invalidate an in-flight reload.
	void RequestChunkRangeReload(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength, LoadPriority ePriority = LoadPriority::kNormal);
	ChunkRangeReloadState GetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength) const;
	void ResetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);

	// Memory profiling
	MemoryStats GetEagerStats() const;
	MemoryStats GetLazyStats() const;
	MemoryStats GetMemoryStats(data::DataTypes eDataType) const;

private:

	std::filesystem::path GetFilePath(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);
	bool CommitAtomicWrite(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, bool bWriteSucceeded);
	void BackupExistingFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename);

	std::filesystem::path mAppDataDirectory;
	std::filesystem::path mTempDirectory;

	// Packed-asset chunk engine. Owns the eager buffers, lazy maps, loading threads, and VirtualAlloc pool; the
	// public chunk methods above forward to it. unique_ptr so PackChunks.h stays out of this header's ~20 PCH
	// consumers (out-of-line ~FileManager in the .cpp destroys it where PackChunks is complete).
	std::unique_ptr<PackChunks> mpPackChunks;
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
	stream.close();
	const bool bGood = !stream.fail();

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
bool WriteVersionedFile(const FileFlags_t& rFlags, const std::filesystem::path& rFilename, const STRUCT_TYPE& rStructure)
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
