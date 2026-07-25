#if defined(BT_CLIENT)

#include "Buffer.h"

namespace engine
{

using enum BufferFlags;

void Buffer::CreateBuffer([[maybe_unused]] std::string_view name, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags, VkMemoryPropertyFlags vkMemoryPropertyFlags, VkBuffer& rVkBuffer, VmaAllocation& rVmaAllocation, VmaAllocationInfo* pVmaAllocationInfo)
{
	VkDeviceSize roundedVkDeviceSize = common::RoundUp(vkDeviceSize, gpInstanceManager->mVkPhysicalDeviceProperties.limits.nonCoherentAtomSize);

	VkBufferCreateInfo vkBufferCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = roundedVkDeviceSize,
		.usage = vkBufferUsageFlags,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
	};

	// Configure VMA allocation based on memory properties
	VmaAllocationCreateInfo vmaAllocationCreateInfo = {};
	vmaAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

	if ((vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
	{
		bool bIsReadbackBuffer = (vkBufferUsageFlags & VK_BUFFER_USAGE_TRANSFER_DST_BIT) != 0u && (vkBufferUsageFlags & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0u;
		bool bIsIndirectBuffer = (vkBufferUsageFlags & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) != 0u;
		if (bIsReadbackBuffer)
		{
			// Readback buffer (GPU→CPU): Must have mapped pointer for CPU reads
			vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
		else if (bIsIndirectBuffer)
		{
			// Indirect buffer: Requires true HOST_VISIBLE + HOST_COHERENT memory for CPU writes read by GPU
			// Do NOT use ALLOW_TRANSFER_INSTEAD - we need guaranteed coherent access without staging
			vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			vmaAllocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		}
		else
		{
			// Upload buffer (CPU→GPU): Allow VMA to use device-local memory with staging if more optimal
			vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
		}
	}

	VmaAllocationInfo vmaAllocationInfo {};
	CHECK_VK(vmaCreateBuffer(gpDeviceManager->mpAllocator, &vkBufferCreateInfo, &vmaAllocationCreateInfo, &rVkBuffer, &rVmaAllocation, &vmaAllocationInfo));
	VkName(VK_OBJECT_TYPE_BUFFER, rVkBuffer, name.data());

	// Optionally return the full VmaAllocationInfo (contains pMappedData if VMA_ALLOCATION_CREATE_MAPPED_BIT was set)
	if (pVmaAllocationInfo != nullptr)
	{
		*pVmaAllocationInfo = vmaAllocationInfo;
	}
}

void Buffer::UploadToDeviceLocal(VkBuffer vkDeviceLocalBuffer, std::span<const DeviceLocalBufferUpload> uploads)
{
	ASSERT(vkDeviceLocalBuffer != VK_NULL_HANDLE);
	ASSERT(!uploads.empty());

	VkDeviceSize vkStagingSize = 0;
	for (const DeviceLocalBufferUpload& rUpload : uploads)
	{
		ASSERT(rUpload.pData != nullptr);
		ASSERT(rUpload.vkSize > 0);
		vkStagingSize += rUpload.vkSize;
	}

	StagingBuffer stagingBuffer("BufferUpload", vkStagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();
	auto pCopies = common::gpThreadLocal->mWorkbuffer.PushBuffer<VkBufferCopy*>(uploads.size() * sizeof(VkBufferCopy));
	VkDeviceSize vkStagingOffset = 0;
	for (size_t iUpload = 0; iUpload < uploads.size(); ++iUpload)
	{
		const DeviceLocalBufferUpload& rUpload = uploads[iUpload];
		std::memcpy(static_cast<std::byte*>(stagingBuffer.vmaAllocationInfo.pMappedData) + vkStagingOffset, rUpload.pData, static_cast<size_t>(rUpload.vkSize));
		pCopies[iUpload] =
		{
			.srcOffset = vkStagingOffset,
			.dstOffset = rUpload.vkDestinationOffset,
			.size = rUpload.vkSize,
		};
		vkStagingOffset += rUpload.vkSize;
	}

	OneShotCommandBuffer oneShotCommandBuffer;
	vkCmdCopyBuffer(oneShotCommandBuffer.mVkCommandBuffer, stagingBuffer.vkBuffer, vkDeviceLocalBuffer, static_cast<uint32_t>(uploads.size()), pCopies);
	oneShotCommandBuffer.Execute();
}

StagingBuffer::StagingBuffer(std::string_view name, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags)
{
	Buffer::CreateBuffer(name, vkDeviceSize, vkBufferUsageFlags, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vkBuffer, vmaAllocation, &vmaAllocationInfo);
}

StagingBuffer::~StagingBuffer()
{
	vmaDestroyBuffer(gpDeviceManager->mpAllocator, vkBuffer, vmaAllocation);
}

void Buffer::RecordBarriers(VkCommandBuffer vkCommandBuffer, const BarrierInfo* pBarriers, int64_t iBarrierCount)
{
	// Build barrier array and accumulate stage masks
	common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();
	VkPipelineStageFlags combinedSrcStage = 0;
	VkPipelineStageFlags combinedDstStage = 0;

	for (int64_t i = 0; i < iBarrierCount; ++i)
	{
		VkAccessFlags srcAccessMask = VK_ACCESS_NONE_KHR;
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		switch (pBarriers[i].eSource)
		{
			case BufferBarrier::kComputeReadWrite:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;

			default:
				ASSERT(false);
				break;
		}

		VkAccessFlags dstAccessMask = VK_ACCESS_NONE_KHR;
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		switch (pBarriers[i].eDestination)
		{
			case BufferBarrier::kComputeRead:
				dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;

			case BufferBarrier::kUniformBufferRead:
				dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
				dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;

			case BufferBarrier::kStorageBufferRead:
				dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				break;

			case BufferBarrier::kShaderIndirectRead:
				dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
				dstStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
				break;

			default:
				ASSERT(false);
				break;
		}

		combinedSrcStage |= srcStageMask;
		combinedDstStage |= dstStageMask;

		common::gpThreadLocal->mWorkbuffer.PushBack<VkBufferMemoryBarrier>(
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = srcAccessMask,
			.dstAccessMask = dstAccessMask,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = pBarriers[i].vkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		});
	}

	auto vkBufferBarriers = common::gpThreadLocal->mWorkbuffer.Span<VkBufferMemoryBarrier>();
	vkCmdPipelineBarrier(vkCommandBuffer, combinedSrcStage, combinedDstStage, 0, 0, nullptr, static_cast<uint32_t>(vkBufferBarriers.size()), vkBufferBarriers.data(), 0, nullptr);
}

Buffer::Buffer(const BufferInfo& rInfo, const std::function<void(void*)>& rDataFunction)
{
	Create(rInfo, rDataFunction);
}

Buffer::Buffer(Buffer&& rOther) noexcept
	: mInfo(std::exchange(rOther.mInfo, {}))
	, mHostVisibleVkBuffer(std::exchange(rOther.mHostVisibleVkBuffer, VK_NULL_HANDLE))
	, mHostVisibleVmaAllocation(std::exchange(rOther.mHostVisibleVmaAllocation, VK_NULL_HANDLE))
	, mpMappedMemory(std::exchange(rOther.mpMappedMemory, nullptr))
	, mDeviceLocalVkBuffer(std::exchange(rOther.mDeviceLocalVkBuffer, VK_NULL_HANDLE))
	, mDeviceLocalVmaAllocation(std::exchange(rOther.mDeviceLocalVmaAllocation, VK_NULL_HANDLE))
{
}

Buffer& Buffer::operator=(Buffer&& rOther) noexcept
{
	if (this != &rOther)
	{
		Destroy();

		mInfo = std::exchange(rOther.mInfo, {});
		mHostVisibleVkBuffer = std::exchange(rOther.mHostVisibleVkBuffer, VK_NULL_HANDLE);
		mHostVisibleVmaAllocation = std::exchange(rOther.mHostVisibleVmaAllocation, VK_NULL_HANDLE);
		mpMappedMemory = std::exchange(rOther.mpMappedMemory, nullptr);
		mDeviceLocalVkBuffer = std::exchange(rOther.mDeviceLocalVkBuffer, VK_NULL_HANDLE);
		mDeviceLocalVmaAllocation = std::exchange(rOther.mDeviceLocalVmaAllocation, VK_NULL_HANDLE);
	}

	return *this;
}

Buffer::~Buffer()
{
	Destroy();
}

void Buffer::Create(const BufferInfo& rInfo, const std::function<void(void*)>& rDataFunction)
{
	Destroy();

	mInfo = rInfo;
	ASSERT(mInfo.name.size() > 0);
	if (!(mInfo.flags & kUniform || mInfo.flags & kStorage))
	{
		ASSERT(mInfo.iVertexStride != 0);
	}

	if (mInfo.flags & kUniform || mInfo.flags & kStorage)
	{
		VkBufferUsageFlagBits vkBufferUsageFlagBits = mInfo.flags & kUniform ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (mInfo.flags & kHostVisible || mInfo.flags & kCopyToDeviceLocalEveryFrame)
		{
			VmaAllocationInfo vmaAllocationInfo {};
			if (mInfo.flags & kHostVisible)
			{
				Buffer::CreateBuffer(mInfo.name, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVmaAllocation, &vmaAllocationInfo);
			}
			else
			{
				Buffer::CreateBuffer(mInfo.name, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVmaAllocation, &vmaAllocationInfo);
			}

			// Use VMA's pre-mapped pointer (VMA_ALLOCATION_CREATE_MAPPED_BIT auto-maps the memory)
			mpMappedMemory = static_cast<char*>(vmaAllocationInfo.pMappedData);

			// Initialize host-visible buffer if rDataFunction provided
			if (rDataFunction != nullptr && mpMappedMemory != nullptr)
			{
				rDataFunction(mpMappedMemory);
			}
		}

		if (mInfo.flags & kDeviceLocal || mInfo.flags & kCopyToDeviceLocalEveryFrame)
		{
			Buffer::CreateBuffer(mInfo.name, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVmaAllocation);
		}
	}
	else
	{
		ASSERT(mInfo.flags & kIndexVertex);
		Buffer::CreateBuffer(mInfo.name, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVmaAllocation);
	}

	if (mInfo.flags & kDeviceLocal && rDataFunction != nullptr)
	{
		// Copy to host visible staging buffer
		StagingBuffer stagingBuffer(mInfo.name, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

		// Use VMA's pre-mapped pointer
		rDataFunction(stagingBuffer.vmaAllocationInfo.pMappedData);

		// Copy to device local memory
		OneShotCommandBuffer oneShotCommandBuffer;
		VkBufferCopy vkBufferCopy
		{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = mInfo.dataVkDeviceSize,
		};
		vkCmdCopyBuffer(oneShotCommandBuffer.mVkCommandBuffer, stagingBuffer.vkBuffer, mDeviceLocalVkBuffer, 1, &vkBufferCopy);
		oneShotCommandBuffer.Execute();
	}
}

void Buffer::Destroy() noexcept
{
	if (mHostVisibleVkBuffer != VK_NULL_HANDLE)
	{
		mpMappedMemory = nullptr;
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mHostVisibleVkBuffer, mHostVisibleVmaAllocation);
		mHostVisibleVkBuffer = VK_NULL_HANDLE;
		mHostVisibleVmaAllocation = VK_NULL_HANDLE;
	}

	if (mDeviceLocalVkBuffer != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mDeviceLocalVkBuffer, mDeviceLocalVmaAllocation);
		mDeviceLocalVkBuffer = VK_NULL_HANDLE;
		mDeviceLocalVmaAllocation = VK_NULL_HANDLE;
	}
}

VkBuffer Buffer::GetBuffer()
{
	return mInfo.flags & kHostVisible ? mHostVisibleVkBuffer : mDeviceLocalVkBuffer;
}

void Buffer::RecordBindVertexBuffer(VkCommandBuffer vkCommandBuffer)
{
	ASSERT(!(mInfo.flags & kUniform));

	vkCmdBindIndexBuffer(vkCommandBuffer, mDeviceLocalVkBuffer, 0, mInfo.vkIndexType);
	int64_t iIndexSize = mInfo.vkIndexType == VK_INDEX_TYPE_UINT16 ? sizeof(uint16_t) : sizeof(uint32_t);
	VkDeviceSize uiVerticesOffset = common::ModelHeader::VerticesOffset(mInfo.iCount, iIndexSize);
	vkCmdBindVertexBuffers(vkCommandBuffer, 0, 1, &mDeviceLocalVkBuffer, &uiVerticesOffset);
}

void Buffer::RecordCopy(VkCommandBuffer vkCommandBuffer, VkPipelineStageFlags stageFlags)
{
	ASSERT((mInfo.flags & kUniform || mInfo.flags & kStorage) && mInfo.flags & kCopyToDeviceLocalEveryFrame);

	// Pre-copy barrier: Wait for shader reads to complete before transfer write
	VkBufferMemoryBarrier vkBufferMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = mDeviceLocalVkBuffer,
		.offset = 0,
		.size = mInfo.dataVkDeviceSize,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, stageFlags, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &vkBufferMemoryBarrier, 0, nullptr);

	VkBufferCopy vkBufferCopy
	{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = mInfo.dataVkDeviceSize,
	};
	vkCmdCopyBuffer(vkCommandBuffer, mHostVisibleVkBuffer, mDeviceLocalVkBuffer, 1, &vkBufferCopy);

	// Post-copy barrier: Transfer write complete before shader reads
	vkBufferMemoryBarrier =
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = mDeviceLocalVkBuffer,
		.offset = 0,
		.size = mInfo.dataVkDeviceSize,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, stageFlags, 0, 0, nullptr, 1, &vkBufferMemoryBarrier, 0, nullptr);
}

} // namespace engine

#endif // defined(BT_CLIENT)
