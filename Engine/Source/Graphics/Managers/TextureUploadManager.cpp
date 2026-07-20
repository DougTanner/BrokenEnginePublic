#if defined(BT_CLIENT)

#include "TextureUploadManager.h"

namespace engine
{

TextureUploadManager::TextureUploadManager()
{
	ASSERT(gpTextureUploadManager == nullptr);

	gpTextureUploadManager = this;
}

TextureUploadManager::~TextureUploadManager()
{
	if (gpTextureUploadManager == this)
	{
		gpTextureUploadManager = nullptr;
	}
}

void TextureUploadManager::InitTransferResources()
{
	RethrowException();

	mbShutdown = false;
	// Re-arm the drain handshake state alongside mbShutdown so a recreate starts the upload thread clean
	//   (WaitIdle always returns with these false today, but keep the re-init symmetric and future-proof).
	mbDrainRequested = false;
	mbDrained = false;
	mbThreadExited.store(false, std::memory_order_release); // A recreated thread starts un-exited

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
	Buffer::CreateBuffer("TransferStaging", kiByteBudgetPerFrame, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mStagingVkBuffer, mStagingVmaAllocation, &stagingVmaAllocationInfo);
	mStagingSize = kiByteBudgetPerFrame;
	mStagingMappedData = stagingVmaAllocationInfo.pMappedData;
}

void TextureUploadManager::DestroyTransferResources()
{
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		return;
	}

	// Join upload thread first. Drain any pending permit before the release: a WaitIdle that returned early via
	//   mbThreadExited (device-loss or fatal-error thread-exit path) can leave its probe permit pending, and an unguarded release
	//   would push the binary_semaphore past its max of 1 (UB). Same drain-then-release idiom as WaitIdle.
	mbShutdown = true;
	std::ignore = mFrameSignal.try_acquire();
	mFrameSignal.release();
	if (mUploadThread.joinable())
	{
		mUploadThread.join();
	}

	// Reset upload-in-progress state
	ResetUploadProgress();

	// Clear stale upload queue
	{
		std::unique_lock lock(mUploadMutex);
		mUploadQueue = {};
	}

	if (mTransferVkFence != VK_NULL_HANDLE)
	{
		// Intentionally unchecked (teardown drain): the file's only unchecked Vulkan result — do not "fix" with CHECK_VK, which could throw mid-destroy.
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
		}
	}

	vkDestroyFence(gpDeviceManager->mVkDevice, mTransferVkFence, nullptr);
	mTransferVkFence = VK_NULL_HANDLE;

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mTransferVkCommandPool, nullptr);
	mTransferVkCommandPool = VK_NULL_HANDLE;
}

void TextureUploadManager::ResetUploadProgress()
{
	mCurrentCrc = 0;
	muiCurrentLayer = 0;
	muiCurrentMip = 0;
	muiCurrentMipY = 0;
	mCurrentDataOffset = 0;
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
	// Nothing in flight to drain once the thread has been told to exit or was never started (first-boot
	//   Create() calls Destroy() before TextureManager creation runs StartThread(), and mbShutdown starts
	//   false); returning matches the original empty-lock no-op and avoids waiting on an ack that will
	//   never come. joinable() is main-thread-only state here: this thread is the sole starter/joiner.
	if (mbShutdown || mbThreadExited || !mUploadThread.joinable())
	{
		return;
	}

	// Close the upload-thread TOCTOU window. UploadThread consumes mFrameSignal (one instruction) before it
	//   takes mWorkMutex, so a bare lock here could return while the thread is between the acquire and its
	//   vkQueueSubmit on the transfer queue -- racing teardown's vkDeviceWaitIdle (a Vulkan external-
	//   synchronization violation). Post a drain probe and block until the thread acks it from a quiescent
	//   point: any iteration in flight when we take the lock below has finished its submit (the thread holds
	//   mWorkMutex across the whole iteration), and a signal it consumed-but-not-yet-processed is skipped at
	//   the top of its loop (mbDrainRequested), so no further submit issues until the next frame re-signals.
	//   Teardown only -- the steady-state wake stays the binary_semaphore.
	{
		std::unique_lock lock(mWorkMutex);
		mbDrained = false;
		mbDrainRequested = true;
	}

	// Ensure exactly one wake is pending (binary_semaphore max count is 1) so a parked thread observes the
	//   request; try_acquire first so the release cannot exceed the max.
	std::ignore = mFrameSignal.try_acquire();
	mFrameSignal.release();

	std::unique_lock lock(mWorkMutex);
	mIdleConditionVariable.wait(lock, [this] { return mbDrained || mbThreadExited; });
	mbDrainRequested = false;
}

void TextureUploadManager::SignalFrame()
{
	// Drain then release so the binary_semaphore (max 1) can't over-release when the upload thread is a frame
	//   behind (mid-iteration, e.g. SubmitChunkUpload's vkWaitForFences). The dropped wake re-posts next frame --
	//   no work lost. Same drain-then-release idiom as WaitIdle / DestroyTransferResources / WaitForTextures.
	std::ignore = mFrameSignal.try_acquire();
	mFrameSignal.release();
}

void TextureUploadManager::RethrowException()
{
	// Healthy per-frame polling stays lock-free. The acquire pairs with UploadThread's release-store, so a
	// fatal exit publishes mException before this thread takes the mutex and consumes the mailbox.
	if (!mbThreadExited.load(std::memory_order_acquire)) [[likely]]
	{
		return;
	}

	std::unique_lock lock(mWorkMutex);
	if (mException != nullptr) [[unlikely]]
	{
		std::exception_ptr exception = std::exchange(mException, nullptr);
		std::rethrow_exception(exception);
	}
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
			// mWorkMutex is not yet held here (workLock is taken below), so lock it to set the exit flag and
			//   notify under the lock -- a WaitIdle parked on mIdleConditionVariable then sees no lost wakeup.
			{
				std::unique_lock exitLock(mWorkMutex);
				mbThreadExited.store(true, std::memory_order_release);
				mIdleConditionVariable.notify_all();
			}
			break;
		}

		std::unique_lock workLock(mWorkMutex);

		// WaitIdle() drain probe: any real iteration in flight when WaitIdle took mWorkMutex already finished
		//   its submit (we hold mWorkMutex across each iteration), so ack from this quiescent point and re-park
		//   without submitting. Queued requests persist and resume on the next frame's signal.
		if (mbDrainRequested)
		{
			mbDrainRequested = false;
			mbDrained = true;
			mIdleConditionVariable.notify_all();
			// Swallow the pending probe permit so it can't drive one more acquire() -> vkQueueSubmit after
			//   WaitIdle returns (the stray permit would race teardown's vkDeviceWaitIdle). Same idiom as
			//   TextureManager::WaitForTextures; a real frame signal re-posts next frame by design.
			std::ignore = mFrameSignal.try_acquire();
			continue;
		}

		try
		{
			if (!DequeueNextUpload())
			{
				continue;
			}

			LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(mCurrentCrc);

			if (HandleUploadEarlyOut(rLazyChunk))
			{
				continue;
			}

			// Wait for previous submission (fence starts signaled, so first wait is free)
			CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mTransferVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));

			bool bFirstChunk = (mCurrentDataOffset == 0);
			bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;
			VkFormat vkFormat = rLazyChunk.header.textureHeader.vkFormat;
			bool bCompressed = (vkFormat == VK_FORMAT_BC4_UNORM_BLOCK || vkFormat == VK_FORMAT_BC5_UNORM_BLOCK || vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
			const ChunkDimensions dimensions
			{
				.bCubemap = bCubemap,
				.vkFormat = vkFormat,
				.uiBlockHeight = bCompressed ? 4u : 1u,
				.uiArrayLayers = bCubemap ? 6u : 1u,
				.uiMipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels),
				.uiBaseWidth = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth),
				.uiBaseHeight = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight),
			};

			// Trust boundary: TextureHeader dims/mips are on-disk pack bytes that drive a VkImageCreateInfo GPU
			// allocation and the mip-iteration copy loop. Reject implausible values before allocating so a corrupt
			// header cannot drive a hostile VkImage size or run the copy loop off rLazyChunk.pData.
			try
			{
				ValidateTextureDimensions(rLazyChunk, dimensions);
			}
			catch (const common::CorruptStreamException& rException)
			{
				LOG(kLoading, kError, "Corrupt texture chunk {}: {}; marking ready zero-filled", mCurrentCrc, rException.what());
				DEBUG_BREAK();
				rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
				gpFileManager->NotifyChunkCompletion(); // No NotifyChunkAdoptable: nothing valid to adopt (no GPU image created)
				ResetUploadProgress();
				continue;
			}

			// First chunk: create VkImage via VMA
			if (bFirstChunk)
			{
				CreateTransferImage(rLazyChunk, dimensions);
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
				.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = dimensions.uiMipLevels, .baseArrayLayer = 0, .layerCount = dimensions.uiArrayLayers},
			};
			if (bFirstChunk)
			{
				vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
			}

			// Fill staging buffer and record copies
			RecordStagingCopies(rLazyChunk, dimensions);

			bool bDone = (muiCurrentLayer >= dimensions.uiArrayLayers);

			SubmitChunkUpload(rLazyChunk, vkImageMemoryBarrier, bDone);
		}
		catch (DeviceLostException&)
		{
			// Device lost during upload -- DestroyTransferResources will clean up GPU resources.
			// Caught before the fatal catch-all below (DeviceLostException derives from std::exception)
			// so device loss keeps its distinct re-upload recovery rather than the zero-fill soft-fail.
			if (mCurrentCrc != 0)
			{
				LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(mCurrentCrc);
				rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
				NotifyChunkAdoptable(); // kUploading -> kDiskLoaded: arm the pending-adoption counter
				mCurrentCrc = 0;
			}
			// workLock is already held here (this catch lives inside its scope) and mWorkMutex is non-recursive,
			//   so set the exit flag and notify directly -- re-locking would self-deadlock. Unblocks a WaitIdle
			//   parked on the drain probe that this exiting thread would otherwise never ack -- the device-loss
			//   teardown deadlock the mbThreadExited flag closes.
			mbThreadExited.store(true, std::memory_order_release);
			mIdleConditionVariable.notify_all();
			break;
		}
		catch (...)
		{
			// Unexpected failures are process-fatal. Keep the chunk kUploading and preserve any unadopted image;
			// Graphics teardown waits for the device before DestroyTransferResources destroys that image.
			mException = std::current_exception();
			mbThreadExited.store(true, std::memory_order_release);
			mIdleConditionVariable.notify_all();
			break;
		}
	}
}

bool TextureUploadManager::DequeueNextUpload()
{
	// If no active texture, dequeue one. Returns false when the queue is empty (caller re-parks).
	if (mCurrentCrc == 0)
	{
		std::unique_lock lock(mUploadMutex);
		if (mUploadQueue.empty())
		{
			return false;
		}

		mCurrentCrc = mUploadQueue.top().crc;
		mUploadQueue.pop();
	}

	return true;
}

bool TextureUploadManager::HandleUploadEarlyOut(LazyChunk& rLazyChunk)
{
	// Early out: no transfer command pool
	if (mTransferVkCommandPool == VK_NULL_HANDLE)
	{
		DEBUG_BREAK();
		rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
		NotifyChunkAdoptable(); // kUploading -> kDiskLoaded: arm the pending-adoption counter
		gpFileManager->NotifyChunkCompletion();
		mCurrentCrc = 0;
		return true;
	}

	// Early out: same queue (concurrent vkQueueSubmit is not thread-safe)
	if (gpDeviceManager->mTransferVkQueue == gpDeviceManager->mGraphicsVkQueue)
	{
		rLazyChunk.eState.store(ChunkState::kDiskLoaded, std::memory_order_release);
		NotifyChunkAdoptable(); // kUploading -> kDiskLoaded: arm the pending-adoption counter
		gpFileManager->NotifyChunkCompletion();
		mCurrentCrc = 0;
		return true;
	}

	return false;
}

void TextureUploadManager::ValidateTextureDimensions(const LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions)
{
	// TextureHeader dims/mips have no DataFile.h structural maximum, so bound two ways: (1) a sane absolute ceiling
	// well above any shipped asset (kiMaxTextureDimension covers the device maxImageDimension2D class; mips capped at
	// log2(16384)+1) so a hostile value cannot drive a huge VkImage; (2) the chunk's actual data bytes — the full
	// mip chain across all layers must fit rLazyChunk.iDataSize, so the copy loop cannot read off pData. Bounding
	// dims/mips first keeps the ComputeImageByteSize math from overflowing on a hostile input.
	static constexpr int64_t kiMaxTextureDimension = 16384; // VkPhysicalDeviceLimits::maxImageDimension2D guaranteed floor class
	static constexpr int64_t kiMaxMipLevels = 15;           // log2(16384) + 1
	if (rDimensions.uiBaseWidth == 0 || rDimensions.uiBaseHeight == 0 || rDimensions.uiMipLevels == 0
		|| rDimensions.uiBaseWidth > kiMaxTextureDimension || rDimensions.uiBaseHeight > kiMaxTextureDimension
		|| rDimensions.uiMipLevels > kiMaxMipLevels)
	{
		throw common::CorruptStreamException("TextureUploadManager dimensions");
	}

	int64_t iExpectedBytes = common::ComputeImageByteSize(rDimensions.vkFormat, rDimensions.uiBaseWidth, rDimensions.uiBaseHeight, rDimensions.uiMipLevels, rDimensions.uiArrayLayers, 1);
	if (iExpectedBytes <= 0 || iExpectedBytes > rLazyChunk.iDataSize)
	{
		throw common::CorruptStreamException("TextureUploadManager size");
	}
}

void TextureUploadManager::CreateTransferImage(LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions)
{
	VkImageCreateInfo vkImageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = rDimensions.bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
		.imageType = VK_IMAGE_TYPE_2D,
		.format = rDimensions.vkFormat,
		.extent = VkExtent3D {rDimensions.uiBaseWidth, rDimensions.uiBaseHeight, 1},
		.mipLevels = rDimensions.uiMipLevels,
		.arrayLayers = rDimensions.uiArrayLayers,
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
	CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &rLazyChunk.vkImage, &rLazyChunk.vmaAllocation, nullptr));
}

void TextureUploadManager::RecordStagingCopies(LazyChunk& rLazyChunk, const ChunkDimensions& rDimensions)
{
	const std::byte* pData = rLazyChunk.pData;
	VkDeviceSize vkStagingUsed = 0;

	while (vkStagingUsed < mStagingSize && muiCurrentLayer < rDimensions.uiArrayLayers)
	{
		uint32_t uiMipWidth = std::max(rDimensions.uiBaseWidth >> muiCurrentMip, 1u);
		uint32_t uiMipHeight = std::max(rDimensions.uiBaseHeight >> muiCurrentMip, 1u);
		uint32_t uiRemainingHeight = uiMipHeight - muiCurrentMipY;
		int64_t iRemainingMipBytes = common::SizeInBytes(rDimensions.vkFormat, uiMipWidth, uiRemainingHeight);
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
			if (muiCurrentMip >= rDimensions.uiMipLevels)
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
			uint32_t uiChunkHeight = std::max(1u, gpInstanceManager->mTransferImageGranularity.height) * rDimensions.uiBlockHeight;
			int64_t iBytesPerChunk = common::SizeInBytes(rDimensions.vkFormat, uiMipWidth, uiChunkHeight);
			int64_t iChunksThatFit = static_cast<int64_t>(vkRemainingStaging) / iBytesPerChunk;
			if (iChunksThatFit == 0)
			{
				break;
			}

			uint32_t uiCopyHeight = static_cast<uint32_t>(iChunksThatFit * uiChunkHeight);
			int64_t iCopyBytes = common::SizeInBytes(rDimensions.vkFormat, uiMipWidth, uiCopyHeight);

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
}

void TextureUploadManager::SubmitChunkUpload(LazyChunk& rLazyChunk, VkImageMemoryBarrier& rVkImageMemoryBarrier, bool bDone)
{
	// Post-copy barrier on final chunk
	if (bDone)
	{
		bool bSeparateTransferFamily = gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex;
		if (bSeparateTransferFamily)
		{
			if (gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kTransferQueueFamilyOwnershipTransferOptional)
			{
				// QFOT optional (VK_KHR_maintenance9): no ownership transfer needed, transition layout directly
				rVkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				rVkImageMemoryBarrier.dstAccessMask = 0;
				rVkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				rVkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				rVkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				rVkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			}
			else
			{
				// Queue family release barrier (transfer -> graphics)
				rVkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				rVkImageMemoryBarrier.dstAccessMask = 0;
				rVkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				rVkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				rVkImageMemoryBarrier.srcQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex);
				rVkImageMemoryBarrier.dstQueueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
			}
			vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &rVkImageMemoryBarrier);
		}
		else
		{
			// Same family: transition directly to SHADER_READ_ONLY
			rVkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			rVkImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			rVkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			rVkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			rVkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			rVkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			vkCmdPipelineBarrier(mTransferVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &rVkImageMemoryBarrier);
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
		NotifyChunkAdoptable(); // kUploading -> kGpuUploadComplete: arm the pending-adoption counter
		gpFileManager->NotifyChunkCompletion();

		ResetUploadProgress();
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
