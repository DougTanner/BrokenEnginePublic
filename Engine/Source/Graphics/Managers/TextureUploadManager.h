#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class TextureUploadManager
{
public:

	TextureUploadManager();
	~TextureUploadManager();

	TextureUploadManager(const TextureUploadManager&) = delete; // Owns a raw std::thread whose lambda captures `this`; deleting copy also suppresses the implicit move
	TextureUploadManager& operator=(const TextureUploadManager&) = delete;

	void InitTransferResources();
	void DestroyTransferResources();
	void StartThread();

	void RequestUpload(common::crc_t crc, LoadPriority ePriority);
	void WaitIdle();
	void SignalFrame();
	void RethrowException();

	// Pending-adoption counter: tracks chunks in an adoptable state (kDiskLoaded / kGpuUploadComplete) awaiting
	// TextureManager::ProcessPendingTextures. Lives here (not on TextureManager) because this manager outlives the
	// device-loss Graphics recreate that destroys TextureManager — so the upload thread never touches a freed owner
	// and the count survives the recreate (FileManager::ResetTextureChunkStates re-arms it through device loss).
	// Relaxed ordering: the count is decoupled from chunk-data visibility (eState's own acquire/release carries that);
	// the worst case is one frame of adoption latency, harmless under lazy texture loading.
	void NotifyChunkAdoptable() noexcept { miPendingAdoptions.fetch_add(1, std::memory_order_relaxed); }
	void NotifyChunkAdopted() noexcept { miPendingAdoptions.fetch_sub(1, std::memory_order_relaxed); }
	bool HasPendingAdoptions() const noexcept { return miPendingAdoptions.load(std::memory_order_relaxed) != 0; }

	std::binary_semaphore mFrameSignal {0};

private:

	void UploadThread();

	// UploadThread seam helpers. Per-chunk dimensions are derived once from the chunk header and passed to
	// the image-create and staging-copy steps.
	struct ChunkDimensions
	{
		bool bCubemap;
		VkFormat vkFormat;
		uint32_t uiBlockHeight;
		uint32_t uiArrayLayers;
		uint32_t uiMipLevels;
		uint32_t uiBaseWidth;
		uint32_t uiBaseHeight;
	};
	bool DequeueNextUpload();
	bool HandleUploadEarlyOut(LazyChunk& rLazyChunk);
	// Trust boundary: throws common::CorruptStreamException if the on-disk TextureHeader dims/mips are implausible.
	static void ValidateTextureDimensions(const LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions);
	void CreateTransferImage(LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions);
	void RecordStagingCopies(LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions);
	void SubmitChunkUpload(LazyChunk& rLazyChunk, VkImageMemoryBarrier& rVkImageMemoryBarrier, bool bDone);

	static constexpr int64_t kiByteBudgetPerFrame = 4 * 1024 * 1024;

	// Zeroes the five "In-progress upload state" fields below (full reset between textures / on teardown / corruption)
	void ResetUploadProgress();

	// In-progress upload state (persists across frames for one texture at a time)
	common::crc_t mCurrentCrc = 0;
	uint32_t muiCurrentLayer = 0;
	uint32_t muiCurrentMip = 0;
	uint32_t muiCurrentMipY = 0;      // Y texel offset within current mip (for sub-mip partial copies)
	size_t mCurrentDataOffset = 0;     // Byte offset into LazyChunk.pData

	std::thread mUploadThread;
	std::mutex mWorkMutex;
	std::condition_variable mIdleConditionVariable; // WaitIdle() waits on this; UploadThread notifies when it acks a drain probe (guarded by mWorkMutex)
	bool mbDrainRequested = false; // WaitIdle() sets this; UploadThread acks instead of submitting (guarded by mWorkMutex)
	bool mbDrained = false; // UploadThread sets this to confirm it reached a quiescent point (guarded by mWorkMutex)
	std::exception_ptr mException; // Unexpected upload-thread failure, published before mbThreadExited while mWorkMutex is held
	std::atomic<bool> mbThreadExited {false}; // UploadThread release-stores this on every loop exit (under mWorkMutex so the CV wait sees no lost wakeup); WaitIdle/RethrowException acquire-load it lock-free
	std::mutex mUploadMutex;
	std::priority_queue<LoadRequest> mUploadQueue;
	std::atomic<bool> mbShutdown {false};
	std::atomic<int64_t> miPendingAdoptions {0};

	VkCommandPool mTransferVkCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer mTransferVkCommandBuffer = VK_NULL_HANDLE;
	VkFence mTransferVkFence = VK_NULL_HANDLE;

	VkBuffer mStagingVkBuffer = VK_NULL_HANDLE;
	VmaAllocation mStagingVmaAllocation = VK_NULL_HANDLE;
	VkDeviceSize mStagingSize = 0;
	void* mStagingMappedData = nullptr;
};

inline TextureUploadManager* gpTextureUploadManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
