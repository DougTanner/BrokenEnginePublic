#include "TextureUploadManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"

namespace engine
{

TextureUploadManager::TextureUploadManager()
{
	gpTextureUploadManager = this;
}

TextureUploadManager::~TextureUploadManager()
{
	gpTextureUploadManager = nullptr;
}

void TextureUploadManager::InitTransferResources()
{
	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mTransferVkCommandPool));
	VkName(VK_OBJECT_TYPE_COMMAND_POOL, mTransferVkCommandPool, "Transfer");

	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mTransferVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mTransferVkCommandBuffer));

	VkFenceCreateInfo vkFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mTransferVkFence));
	VkName(VK_OBJECT_TYPE_FENCE, mTransferVkFence, "Transfer");
}

void TextureUploadManager::DestroyTransferResources()
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		return;
	}

	// Join upload thread first
	{
		std::unique_lock lock(mUploadMutex);
		mShutdown = true;
	}
	mUploadCondition.notify_one();
	if (mUploadThread.joinable())
	{
		mUploadThread.join();
	}

	// Clean up persistent staging buffer
	if (mStagingVkBuffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mStagingVkBuffer, mStagingVmaAllocation);
		mStagingVkBuffer = VK_NULL_HANDLE;
		mStagingVmaAllocation = VK_NULL_HANDLE;
		mStagingSize = 0;
	}

	// Clean up any GPU-uploaded texture images that were not adopted by TextureManager
	for (const auto& [rCrc, rLazyChunk] : gpFileManager->GetLazyChunkMap())
	{
		if (rLazyChunk.vkImage != VK_NULL_HANDLE)
		{
			LazyChunk& rMutableChunk = gpFileManager->GetLazyChunk(rCrc);
			vmaDestroyImage(gpDeviceManager->mpAllocator, rMutableChunk.vkImage, rMutableChunk.vmaAllocation);
			rMutableChunk.vkImage = VK_NULL_HANDLE;
			rMutableChunk.vmaAllocation = VK_NULL_HANDLE;
			rMutableChunk.vkDeviceMemory = VK_NULL_HANDLE;
		}
	}

	vkDestroyFence(gpDeviceManager->mVkDevice, mTransferVkFence, nullptr);
	mTransferVkFence = VK_NULL_HANDLE;

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mTransferVkCommandPool, nullptr);
	mTransferVkCommandPool = VK_NULL_HANDLE;
}

void TextureUploadManager::StartThread()
{
	Log("TextureUploadManager::StartThread()");
	mUploadThread = std::thread(&TextureUploadManager::UploadThread, this);
}

void TextureUploadManager::RequestUpload(common::crc_t crc, LoadPriority priority)
{
	{
		std::unique_lock lock(mUploadMutex);
		mUploadQueue.push({crc, priority});
	}
	mUploadCondition.notify_one();
}

void TextureUploadManager::ClearTransferredImage(common::crc_t crc)
{
	LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(crc);
	rLazyChunk.vkImage = VK_NULL_HANDLE;
	rLazyChunk.vmaAllocation = VK_NULL_HANDLE;
	rLazyChunk.vkDeviceMemory = VK_NULL_HANDLE;

	// Free CPU data (no longer needed after GPU adoption)
	rLazyChunk.data.clear();
	rLazyChunk.data.shrink_to_fit();
}

void TextureUploadManager::UploadThread()
{
	common::ThreadLocal threadLocal(0, common::kThreadTextureUpload);

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	while (!mShutdown)
	{
		common::crc_t crc = 0;

		{
			std::unique_lock lock(mUploadMutex);

			if (!mShutdown && mUploadQueue.empty())
			{
				mUploadCondition.wait(lock, [this] { return mShutdown || !mUploadQueue.empty(); });
			}

			if (mShutdown)
			{
				break;
			}

			crc = mUploadQueue.top().crc;
			mUploadQueue.pop();
		}

		LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(crc);
		UploadTextureToGpu(crc, rLazyChunk);
	}
}

void TextureUploadManager::UploadTextureToGpu(common::crc_t crc, LazyChunk& rLazyChunk)
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		Log("Chunk {} kUploading -> kDiskLoaded (no transfer command pool)", crc);
		common::DebugBreak();
		rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
		gpFileManager->NotifyChunkCompletion();
		return;
	}

	// Skip background GPU upload when using the same queue as graphics (concurrent vkQueueSubmit is not thread-safe)
	if (gpDeviceManager->mTransferVkQueue == gpDeviceManager->mGraphicsVkQueue)
	{
		Log("Chunk {} kUploading -> kDiskLoaded (same queue family)", crc);
		rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
		gpFileManager->NotifyChunkCompletion();
		return;
	}

	// Wait for previous upload to finish, then reset fence
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNs.count()));
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence));

	// Reset persistent command buffer
	CHECK_VK(vkResetCommandBuffer(mTransferVkCommandBuffer, 0));

	bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;
	uint32_t uiArrayLayers = bCubemap ? 6u : 1u;

	// Create VkImage via VMA
	VkImageCreateInfo vkImageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
		.imageType = VK_IMAGE_TYPE_2D,
		.format = rLazyChunk.header.textureHeader.vkFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth), static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight), 1},
		.mipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels),
		.arrayLayers = uiArrayLayers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo vmaAllocationCreateInfo = {};
	vmaAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	VmaAllocationInfo vmaAllocationInfo {};
	CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &rLazyChunk.vkImage, &rLazyChunk.vmaAllocation, &vmaAllocationInfo));
	rLazyChunk.vkDeviceMemory = vmaAllocationInfo.deviceMemory;

	// Calculate total staging buffer size
	VkDeviceSize vkStagingSize = 0;
	uint32_t uiWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth);
	uint32_t uiHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight);
	uint32_t uiMipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels);
	for (uint32_t i = 0; i < uiMipLevels; ++i)
	{
		vkStagingSize += uiArrayLayers * common::SizeInBytes(rLazyChunk.header.textureHeader.vkFormat, uiWidth, uiHeight);
		uiWidth /= 2;
		uiHeight /= 2;
	}

	// Reuse staging buffer if large enough, otherwise grow it
	if (vkStagingSize > mStagingSize)
	{
		if (mStagingVkBuffer != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(gpDeviceManager->mpAllocator, mStagingVkBuffer, mStagingVmaAllocation);
		}
		VkDeviceMemory stagingVkDeviceMemory = VK_NULL_HANDLE;
		VmaAllocationInfo stagingVmaAllocationInfo {};
		Buffer::CreateBuffer("TransferStaging", vkStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mStagingVkBuffer, stagingVkDeviceMemory, mStagingVmaAllocation, &stagingVmaAllocationInfo);
		mStagingSize = vkStagingSize;
		mStagingMappedData = stagingVmaAllocationInfo.pMappedData;
	}
	memcpy(mStagingMappedData, rLazyChunk.data.data(), vkStagingSize);

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	CHECK_VK(vkBeginCommandBuffer(mTransferVkCommandBuffer, &vkCommandBufferBeginInfo));

	// Barrier: UNDEFINED -> TRANSFER_DST_OPTIMAL
	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = rLazyChunk.vkImage,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = uiMipLevels, .baseArrayLayer = 0, .layerCount = uiArrayLayers},
	};
	vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);

	// Record buffer-to-image copies for all mip levels and array layers
	size_t uiOffset = 0;
	for (uint32_t iLayer = 0; iLayer < uiArrayLayers; ++iLayer)
	{
		uiWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth);
		uiHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight);
		for (uint32_t iMip = 0; iMip < uiMipLevels; ++iMip)
		{
			VkBufferImageCopy vkBufferImageCopy = {};
			vkBufferImageCopy.bufferOffset = uiOffset;
			vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vkBufferImageCopy.imageSubresource.mipLevel = iMip;
			vkBufferImageCopy.imageSubresource.baseArrayLayer = iLayer;
			vkBufferImageCopy.imageSubresource.layerCount = 1;
			vkBufferImageCopy.imageExtent.width = uiWidth;
			vkBufferImageCopy.imageExtent.height = uiHeight;
			vkBufferImageCopy.imageExtent.depth = 1;

			uiOffset += common::SizeInBytes(rLazyChunk.header.textureHeader.vkFormat, uiWidth, uiHeight);
			uiWidth /= 2;
			uiHeight /= 2;

			vkCmdCopyBufferToImage(mTransferVkCommandBuffer, mStagingVkBuffer, rLazyChunk.vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);
		}
	}

	// Post-copy barrier: queue family ownership transfer or layout transition
	bool bSeparateTransferFamily = gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex;
	if (bSeparateTransferFamily)
	{
		if (gpDeviceManager->mbTransferQfotOptional)
		{
			// QFOT optional (VK_KHR_maintenance9): no ownership transfer needed, transition layout directly
			vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkImageMemoryBarrier.dstAccessMask = 0;
			vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			vkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			vkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		}
		else
		{
			// Queue family release barrier (transfer -> graphics)
			vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			vkImageMemoryBarrier.dstAccessMask = 0;
			vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			vkImageMemoryBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex);
			vkImageMemoryBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
		}
		vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
	}
	else
	{
		// Same family: transition directly to SHADER_READ_ONLY
		vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
	}

	CHECK_VK(vkEndCommandBuffer(mTransferVkCommandBuffer));

	// Submit to transfer queue
	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &mTransferVkCommandBuffer,
		.signalSemaphoreCount = 0,
	};
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mTransferVkQueue, 1, &vkSubmitInfo, mTransferVkFence));

	// Wait for transfer to complete before signaling kGpuUploadComplete, ensuring the release barrier
	// is finished on the GPU before the graphics queue records an acquire barrier
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNs.count()));

	// Signal GPU upload complete (atomic store with release semantics)
	Log("Chunk {} kUploading -> kGpuUploadComplete", crc);
	rLazyChunk.eState.store(ChunkState::kGpuUploadComplete, std::memory_order_release);

	// Wake WaitForChunks waiters
	gpFileManager->NotifyChunkCompletion();
}

} // namespace engine
