#pragma once

#include "FileManager.h" // EagerChunk/LazyChunk/ChunkState/MovableAtomicChunkState/LoadRequest/LoadPriority/MemoryStats + IsEagerChunk/IsServerChunk decls live here

namespace engine
{

// The packed-asset chunk engine, owned by FileManager via std::unique_ptr.
// Holds the eager pack buffers, the lazy chunk maps + atomic eState machine, the background loading-thread pool
// and its sync primitives, and the single-VirtualAlloc lazy memory pool. Not a *Manager: no gp* global, not
// aggregated into Engine.h; included only by PackChunks.cpp and FileManager.cpp.
class PackChunks
{
public:

	explicit PackChunks(const std::filesystem::path& rDataDirectory);
	~PackChunks();

	PackChunks(const PackChunks&) = delete; // Owns std::threads whose lambdas capture `this`; deleting copy also suppresses the implicit move
	PackChunks& operator=(const PackChunks&) = delete;

	const std::unordered_map<common::crc_t, EagerChunk>& GetEagerChunkMap() const;
	const std::unordered_map<common::crc_t, LazyChunk>& GetLazyChunkMap() const;
	common::crc_t GetPackIntegrityToken() const;

	// Lazy loading APIs
	bool IsChunkReady(common::crc_t crc) const;
	void RequestChunkLoad(std::span<const common::crc_t> crcs, LoadPriority ePriority);
	void WaitForChunks(std::span<const common::crc_t> crcs);

	// Streaming API for reading data at specific offset within a chunk
	bool ReadChunkData(common::crc_t crc, uint64_t uiOffset, std::span<std::byte> buffer);

	// Notification for chunk completion (wakes WaitForChunks waiters)
	void NotifyChunkCompletion();
	LazyChunk& GetLazyChunk(common::crc_t crc);

	void ResetTextureChunkStates();
	void ResetTextureChunkStates(std::span<const common::crc_t> targetCrcs);

	void DecommitChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);
	[[nodiscard]] bool RecommitAndReloadChunkRange(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);
	void RequestChunkRangeReload(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength, LoadPriority ePriority);
	ChunkRangeReloadState GetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength) const;
	void ResetChunkRangeReloadState(common::crc_t crc, uint64_t uiOffset, uint64_t uiLength);

	// Memory profiling
	MemoryStats GetEagerStats() const;
	MemoryStats GetLazyStats() const;
	MemoryStats GetMemoryStats(data::DataTypes eDataType) const;

private:

	void LoadPackFiles();
	void LoadingThread(int64_t iThreadIndex);
	void LoadChunk(const LoadRequest& rRequest, int64_t iThreadIndex);
	[[nodiscard]] bool RecommitChunkRange(common::crc_t crc, const LazyChunk& rLazyChunk, uint64_t uiOffset, uint64_t uiLength);
	std::filesystem::path GetDataFilePath(data::DataTypes eDataType, std::string_view extension) const;

	std::filesystem::path mDataDirectory;

	// Cached pack file paths (initialized once in LoadPackFiles)
	std::filesystem::path mPackFilePaths[data::kDataTypeCount];

	// Only available for eager pack files
	std::vector<std::byte> mPackFileData[data::kDataTypeCount];

	// Per-data-type chunk-location tables (offset/size/path CRC/content CRC), read from each manifest in LoadPackFiles
	std::vector<common::ChunkLocation> mChunkLocations[data::kDataTypeCount];
	common::crc_t mPackIntegrityToken = common::kCrcSeed;

	// Split chunk maps for eager and lazy loading
	std::unordered_map<common::crc_t, EagerChunk> mEagerChunkMap;  // Scene, Model, Shader, Raw
	std::unordered_map<common::crc_t, LazyChunk> mLazyChunkMap;  // Audio, Islands, Texture

	// N background loading threads, assigned inside the async eager-load task (mLoadingFuture), not the ctor body.
	// Each LoadingThread pops from the shared priority queue and reads the sync members below
	// (mWakeCondition/mQueueMutex/mRequestQueue/mShutdown), owning a private read buffer + decompress scratch
	// (indexed by thread index). ~PackChunks first drains mLoadingFuture (ensuring these assignments have
	// happened), then sets mShutdown + notify_all()s + join()s every thread before those members destruct.
	// Count is deliberately small: each thread doubles the read-buffer + decompress-scratch memory footprint.
	static constexpr int64_t kiLoadingThreadCount = 2;
	std::thread mLoadingThreads[kiLoadingThreadCount];
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

	// Per-loading-thread sector-aligned read buffers (one per thread, indexed by thread index; reused across that
	// thread's chunk reads). Size is shared — identical for every thread.
	std::byte* mpReadBuffers[kiLoadingThreadCount] {};
	int64_t miReadBufferSize = 0;
	int64_t miSectorSize = 0;
	int64_t miPageSize = 0; // VM page granularity for lazy-chunk sub-range decommit/recommit

	// Pre-allocated memory pool for all lazy chunk data (VirtualAlloc MEM_COMMIT — committed, not pre-faulted)
	std::byte* mpLazyPool = nullptr;
	int64_t miLazyPoolSize = 0;

	// Per-loading-thread scratch buffers for compressed chunks (LZ4 or zlib; one per thread, indexed by thread index).
	// Each sized at boot to the largest compressed chunk on disk; reused per chunk. Size is shared.
	std::byte* mpDecompressScratches[kiLoadingThreadCount] {};
	int64_t miDecompressScratchSize = 0;

	// Sub-read size for chunked disk reads (256KB balances NVMe throughput vs L3 cache pressure)
	static constexpr int64_t kiSubReadSize = 256 * 1024;
};

} // namespace engine
