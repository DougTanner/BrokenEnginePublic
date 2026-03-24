#pragma once

namespace engine
{

class TextureUploadManager
{
public:

	TextureUploadManager();
	~TextureUploadManager();

	void InitTransferResources();
	void DestroyTransferResources();
	void StartThread();

	void RequestUpload(common::crc_t crc, LoadPriority ePriority);
	void WaitIdle();

	std::binary_semaphore mFrameSignal {0};

private:

	void UploadThread();

	static constexpr int64_t kiByteBudgetPerFrame = 4 * 1024 * 1024;

	// In-progress upload state (persists across frames for one texture at a time)
	common::crc_t mCurrentCrc = 0;
	uint32_t muiCurrentLayer = 0;
	uint32_t muiCurrentMip = 0;
	uint32_t muiCurrentMipY = 0;      // Y texel offset within current mip (for sub-mip partial copies)
	size_t mCurrentDataOffset = 0;     // Byte offset into LazyChunk.pData

	std::thread mUploadThread;
	std::mutex mWorkMutex;
	std::mutex mUploadMutex;
	std::priority_queue<LoadRequest> mUploadQueue;
	std::atomic<bool> mbShutdown {false};

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
