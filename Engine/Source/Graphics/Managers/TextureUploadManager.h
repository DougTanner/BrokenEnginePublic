#pragma once

namespace engine
{

struct LazyChunk;

class TextureUploadManager
{
public:

	TextureUploadManager();
	~TextureUploadManager();

	void InitTransferResources();
	void DestroyTransferResources();
	void StartThread();

	void RequestUpload(common::crc_t crc);
	void ClearTransferredImage(common::crc_t crc);

private:

	void UploadThread();
	void UploadTextureToGpu(LazyChunk& rLazyChunk);

	std::thread mUploadThread;
	std::condition_variable mUploadCondition;
	std::mutex mUploadMutex;
	std::vector<common::crc_t> mUploadQueue;
	std::atomic<bool> mShutdown {false};

	VkCommandPool mTransferVkCommandPool = VK_NULL_HANDLE;
	VkFence mTransferVkFence = VK_NULL_HANDLE;
};

inline TextureUploadManager* gpTextureUploadManager = nullptr;

} // namespace engine
