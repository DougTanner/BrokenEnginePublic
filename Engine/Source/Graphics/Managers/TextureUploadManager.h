#pragma once

#include "File/FileManager.h"

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

	void RequestUpload(common::crc_t crc, LoadPriority priority);
	void ClearTransferredImage(common::crc_t crc);

private:

	void UploadThread();
	void UploadTextureToGpu(common::crc_t crc, LazyChunk& rLazyChunk);

	std::thread mUploadThread;
	std::condition_variable mUploadCondition;
	std::mutex mUploadMutex;
	std::priority_queue<LoadRequest> mUploadQueue;
	std::atomic<bool> mShutdown {false};

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
