#if defined(BT_CLIENT)

#include "TextureUploadManager.h"

#include "Memory/GlobalAllocator.h"

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
	mbShutdown = false;

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

	VmaAllocationInfo stagingVmaAllocationInfo {};
	VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;
	Buffer::CreateBuffer("TransferStaging", kiByteBudgetPerFrame, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mStagingVkBuffer, vkDeviceMemory, mStagingVmaAllocation, &stagingVmaAllocationInfo);
	mStagingSize = kiByteBudgetPerFrame;
	mStagingMappedData = stagingVmaAllocationInfo.pMappedData;
}

void TextureUploadManager::DestroyTransferResources()
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		return;
	}

	// Join upload thread first
	mbShutdown = true;
	mFrameSignal.release();
	if (mUploadThread.joinable())
	{
		mUploadThread.join();
	}

	// Reset upload-in-progress state
	mCurrentCrc = 0;
	muiCurrentLayer = 0;
	muiCurrentMip = 0;
	muiCurrentMipY = 0;
	mCurrentDataOffset = 0;

	// Clear stale upload queue
	{
		std::unique_lock lock(mUploadMutex);
		mUploadQueue = {};
	}

	if (mTransferVkFence != VK_NULL_HANDLE)
	{
		vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count());
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
	if (mUploadThread.joinable())
	{
		return;
	}
	mUploadThread = std::thread(&TextureUploadManager::UploadThread, this);
}

void TextureUploadManager::RequestUpload(common::crc_t crc, LoadPriority ePriority)
{
	{
		std::unique_lock lock(mUploadMutex);

		// Heap: priority_queue insertion may allocate. Items must persist until the upload thread pops them,
		//   so a workbuffer (frame-scoped) can't own them, and the queue grows/shrinks unpredictably
		ScopedSuppressAllocationTracking suppress;

		mUploadQueue.push({crc, ePriority});
	}
}

void TextureUploadManager::WaitIdle()
{
	std::unique_lock lock(mWorkMutex);
}

void TextureUploadManager::UploadThread()
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

	common::ThreadLocal threadLocal(1024, common::kThreadTextureUpload);

	while (!mbShutdown)
	{
		// Wait for the main thread to signal one chunk this frame
		mFrameSignal.acquire();
		if (mbShutdown)
		{
			break;
		}

		std::unique_lock workLock(mWorkMutex);

		try
		{
			// If no active texture, dequeue one
			if (mCurrentCrc == 0)
			{
				std::unique_lock lock(mUploadMutex);
				if (mUploadQueue.empty())
				{
					continue;
				}

				mCurrentCrc = mUploadQueue.top().crc;
				mUploadQueue.pop();
			}

			LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(mCurrentCrc);

			// Early out: no transfer command pool
			if (mTransferVkCommandPool == VK_NULL_HANDLE)
			{
				DEBUG_BREAK();
				rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
				gpFileManager->NotifyChunkCompletion();
				mCurrentCrc = 0;
				continue;
			}

			// Early out: same queue (concurrent vkQueueSubmit is not thread-safe)
			if (gpDeviceManager->mTransferVkQueue == gpDeviceManager->mGraphicsVkQueue)
			{
				rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
				gpFileManager->NotifyChunkCompletion();
				mCurrentCrc = 0;
				continue;
			}

			// Wait for previous submission (fence starts signaled, so first wait is free)
			CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));

			bool bFirstChunk = (mCurrentDataOffset == 0);
			bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;
			uint32_t uiArrayLayers = bCubemap ? 6u : 1u;
			uint32_t uiMipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels);
			VkFormat vkFormat = rLazyChunk.header.textureHeader.vkFormat;
			bool bCompressed = (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK || vkFormat == VK_FORMAT_BC5_UNORM_BLOCK || vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
			uint32_t uiBlockHeight = bCompressed ? 4u : 1u;
			uint32_t uiBaseWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth);
			uint32_t uiBaseHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight);

			// First chunk: create VkImage via VMA
			if (bFirstChunk)
			{
				VkImageCreateInfo vkImageCreateInfo
				{
					.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
					.pNext = nullptr,
					.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
					.imageType = VK_IMAGE_TYPE_2D,
					.format = vkFormat,
					.extent = VkExtent3D {uiBaseWidth, uiBaseHeight, 1},
					.mipLevels = uiMipLevels,
					.arrayLayers = uiArrayLayers,
					.samples = VK_SAMPLE_COUNT_1_BIT,
					.tiling = VK_IMAGE_TILING_OPTIMAL,
					.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
					.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
					.queueFamilyIndexCount = 0,
					.pQueueFamilyIndices = nullptr,
					.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				};
				VmaAllocationCreateInfo vmaAllocationCreateInfo {};
				vmaAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
				VmaAllocationInfo vmaAllocationInfo {};
				CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &rLazyChunk.vkImage, &rLazyChunk.vmaAllocation, &vmaAllocationInfo));
				rLazyChunk.vkDeviceMemory = vmaAllocationInfo.deviceMemory;
			}

			// Begin command buffer
			CHECK_VK(vkResetCommandBuffer(mTransferVkCommandBuffer, 0));
			VkCommandBufferBeginInfo vkCommandBufferBeginInfo
			{
				.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
				.pNext = nullptr,
				.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
				.pInheritanceInfo = nullptr,
			};
			CHECK_VK(vkBeginCommandBuffer(mTransferVkCommandBuffer, &vkCommandBufferBeginInfo));

			// First chunk: transition UNDEFINED -> TRANSFER_DST_OPTIMAL (entire image)
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
			if (bFirstChunk)
			{
				vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
			}

			// Fill staging buffer and record copies
			const std::byte* pData = rLazyChunk.pData;
			VkDeviceSize vkStagingUsed = 0;

			while (vkStagingUsed < mStagingSize && muiCurrentLayer < uiArrayLayers)
			{
				uint32_t uiMipWidth = std::max(uiBaseWidth >> muiCurrentMip, 1u);
				uint32_t uiMipHeight = std::max(uiBaseHeight >> muiCurrentMip, 1u);
				uint32_t uiRemainingHeight = uiMipHeight - muiCurrentMipY;
				int64_t iRemainingMipBytes = common::SizeInBytes(vkFormat, uiMipWidth, uiRemainingHeight);
				VkDeviceSize vkRemainingStaging = mStagingSize - vkStagingUsed;

				if (iRemainingMipBytes <= static_cast<int64_t>(vkRemainingStaging))
				{
					// Whole remaining mip fits
					std::memcpy(static_cast<std::byte*>(mStagingMappedData) + vkStagingUsed, pData + mCurrentDataOffset, iRemainingMipBytes);

					VkBufferImageCopy vkBufferImageCopy {};
					vkBufferImageCopy.bufferOffset = vkStagingUsed;
					vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vkBufferImageCopy.imageSubresource.mipLevel = muiCurrentMip;
					vkBufferImageCopy.imageSubresource.baseArrayLayer = muiCurrentLayer;
					vkBufferImageCopy.imageSubresource.layerCount = 1;
					vkBufferImageCopy.imageOffset = {0, static_cast<int32_t>(muiCurrentMipY), 0};
					vkBufferImageCopy.imageExtent = {uiMipWidth, uiRemainingHeight, 1};
					vkCmdCopyBufferToImage(mTransferVkCommandBuffer, mStagingVkBuffer, rLazyChunk.vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);

					vkStagingUsed += iRemainingMipBytes;
					mCurrentDataOffset += iRemainingMipBytes;
					muiCurrentMipY = 0;
					++muiCurrentMip;
					if (muiCurrentMip >= uiMipLevels)
					{
						muiCurrentMip = 0;
						++muiCurrentLayer;
					}
				}
				else
				{
					// Partial mip: chunk height must satisfy VUID-vkCmdCopyBufferToImage-imageOffset-07738.
					// The Vulkan validator interprets minImageTransferGranularity as BLOCK-relative for
					// compressed formats, so the chunk's BLOCK extent (chunk_pixels / blockHeight) must
					// be a multiple of granularity.height. For uncompressed (blockHeight == 1) this
					// degenerates to `granularity.height` pixels per chunk; for BCn (blockHeight == 4)
					// it scales up to `granularity.height * 4 = 64` pixels per chunk on hardware that
					// reports granularity.height == 16.
					uint32_t uiChunkHeight = std::max(1u, gpInstanceManager->mTransferImageGranularity.height) * uiBlockHeight;
					int64_t iBytesPerChunk = common::SizeInBytes(vkFormat, uiMipWidth, uiChunkHeight);
					int64_t iChunksThatFit = static_cast<int64_t>(vkRemainingStaging) / iBytesPerChunk;
					if (iChunksThatFit == 0)
					{
						break;
					}

					uint32_t uiCopyHeight = static_cast<uint32_t>(iChunksThatFit * uiChunkHeight);
					int64_t iCopyBytes = common::SizeInBytes(vkFormat, uiMipWidth, uiCopyHeight);

					std::memcpy(static_cast<std::byte*>(mStagingMappedData) + vkStagingUsed, pData + mCurrentDataOffset, iCopyBytes);

					VkBufferImageCopy vkBufferImageCopy {};
					vkBufferImageCopy.bufferOffset = vkStagingUsed;
					vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					vkBufferImageCopy.imageSubresource.mipLevel = muiCurrentMip;
					vkBufferImageCopy.imageSubresource.baseArrayLayer = muiCurrentLayer;
					vkBufferImageCopy.imageSubresource.layerCount = 1;
					vkBufferImageCopy.imageOffset = {0, static_cast<int32_t>(muiCurrentMipY), 0};
					vkBufferImageCopy.imageExtent = {uiMipWidth, uiCopyHeight, 1};
					vkCmdCopyBufferToImage(mTransferVkCommandBuffer, mStagingVkBuffer, rLazyChunk.vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);

					mCurrentDataOffset += iCopyBytes;
					muiCurrentMipY += uiCopyHeight;
					break; // staging full
				}
			}

			bool bDone = (muiCurrentLayer >= uiArrayLayers);

			// Post-copy barrier on final chunk
			if (bDone)
			{
				bool bSeparateTransferFamily = gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex;
				if (bSeparateTransferFamily)
				{
					if (gpDeviceManager->mbTransferQueueFamilyOwnershipTransferOptional)
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
				.pSignalSemaphores = nullptr,
			};
			CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence));
			CHECK_VK(vkQueueSubmit(gpDeviceManager->mTransferVkQueue, 1, &vkSubmitInfo, mTransferVkFence));

			if (bDone)
			{
				// Wait for GPU to finish before signaling completion
				CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));

				rLazyChunk.eState.store(ChunkState::kGpuUploadComplete, std::memory_order_release);
				gpFileManager->NotifyChunkCompletion();

				mCurrentCrc = 0;
				mCurrentDataOffset = 0;
				muiCurrentLayer = 0;
				muiCurrentMip = 0;
				muiCurrentMipY = 0;
			}
		}
		catch (DeviceLostException&)
		{
			// Device lost during upload -- DestroyTransferResources will clean up GPU resources
			if (mCurrentCrc != 0)
			{
				LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(mCurrentCrc);
				rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
				mCurrentCrc = 0;
			}
			break;
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
