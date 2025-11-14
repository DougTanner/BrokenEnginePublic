#include "Buffer.h"

#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"

namespace engine
{

using enum BufferFlags;

void Buffer::CreateBuffer([[maybe_unused]] std::string_view pcName, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags, VkMemoryPropertyFlags vkMemoryPropertyFlags, VkBuffer& rVkBuffer, VkDeviceMemory& rVkDeviceMemory, VmaAllocation& rVmaAllocation, VmaAllocationInfo* pVmaAllocationInfo)
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

	if (vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
	{
		// Allow VMA to use device-local memory with staging if more optimal
		vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
	}

	VmaAllocationInfo vmaAllocationInfo {};
	CHECK_VK(vmaCreateBuffer(gpDeviceManager->mpAllocator, &vkBufferCreateInfo, &vmaAllocationCreateInfo, &rVkBuffer, &rVmaAllocation, &vmaAllocationInfo));
	VK_NAME(VK_OBJECT_TYPE_BUFFER, rVkBuffer, pcName.data());

	// Get the VkDeviceMemory for compatibility with existing code that still uses vkMapMemory
	rVkDeviceMemory = vmaAllocationInfo.deviceMemory;

	// Optionally return the full VmaAllocationInfo (contains pMappedData if VMA_ALLOCATION_CREATE_MAPPED_BIT was set)
	if (pVmaAllocationInfo != nullptr)
	{
		*pVmaAllocationInfo = vmaAllocationInfo;
	}
}

void Buffer::RecordBarriers(VkCommandBuffer vkCommandBuffer, std::span<const BarrierInfo> barriers)
{
	// Build barrier array and accumulate stage masks
	std::vector<VkBufferMemoryBarrier> vkBufferBarriers;
	vkBufferBarriers.reserve(barriers.size());
	VkPipelineStageFlags combinedSrcStage = 0;
	VkPipelineStageFlags combinedDstStage = 0;

	for (const auto& barrier : barriers)
	{
		VkAccessFlags srcAccessMask = VK_ACCESS_NONE_KHR;
		VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		switch (barrier.eSource)
		{
			case BufferBarrier::kComputeReadWrite:
				srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
				srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				break;

			default:
				throw std::runtime_error("Unhandled case in Buffer::RecordBarriers()");
		}

		VkAccessFlags dstAccessMask = VK_ACCESS_NONE_KHR;
		VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		switch (barrier.eDestination)
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
				throw std::runtime_error("Unhandled case in Buffer::RecordBarriers()");
		}

		combinedSrcStage |= srcStageMask;
		combinedDstStage |= dstStageMask;

		vkBufferBarriers.push_back(
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = srcAccessMask,
			.dstAccessMask = dstAccessMask,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = barrier.vkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		});
	}

	vkCmdPipelineBarrier(vkCommandBuffer, combinedSrcStage, combinedDstStage, 0, 0, nullptr, static_cast<uint32_t>(vkBufferBarriers.size()), vkBufferBarriers.data(), 0, nullptr);
}

Buffer::Buffer(const BufferInfo& rInfo, std::function<void(void*)> dataFunction)
{
	Create(rInfo, dataFunction);
}

Buffer::~Buffer()
{
	Destroy();
}

void Buffer::Create(const BufferInfo& rInfo, std::function<void(void*)> dataFunction)
{
	Destroy();

	mInfo = rInfo;
	ASSERT(mInfo.pcName.size() > 0);
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
				Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVkDeviceMemory, mHostVisibleVmaAllocation, &vmaAllocationInfo);
			}
			else
			{
				Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVkDeviceMemory, mHostVisibleVmaAllocation, &vmaAllocationInfo);
			}

			// Use VMA's pre-mapped pointer (VMA_ALLOCATION_CREATE_MAPPED_BIT auto-maps the memory)
			mpMappedMemory = static_cast<char*>(vmaAllocationInfo.pMappedData);
		}

		if (mInfo.flags & kDeviceLocal || mInfo.flags & kCopyToDeviceLocalEveryFrame)
		{
			Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVkDeviceMemory, mDeviceLocalVmaAllocation);
		}
	}
	else
	{
		ASSERT(mInfo.flags & kIndexVertex);
		Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVkDeviceMemory, mDeviceLocalVmaAllocation);
	}

	if (mInfo.flags & kDeviceLocal)
	{
		// Copy to host visible staging buffer
		VkBuffer vkBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;
		VmaAllocation vmaAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo vmaAllocationInfo {};
		CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vkBuffer, vkDeviceMemory, vmaAllocation, &vmaAllocationInfo);

		// Use VMA's pre-mapped pointer
		dataFunction(vmaAllocationInfo.pMappedData);

		// Copy to device local memory
		OneShotCommandBuffer oneShotCommandBuffer;
		VkBufferCopy vkBufferCopy
		{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = mInfo.dataVkDeviceSize,
		};
		vkCmdCopyBuffer(oneShotCommandBuffer.mVkCommandBuffer, vkBuffer, mDeviceLocalVkBuffer, 1, &vkBufferCopy);
		oneShotCommandBuffer.Execute(true);

		// Clean up
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, vkBuffer, vmaAllocation);
	}
}

void Buffer::Destroy() noexcept
{
	if (mHostVisibleVkDeviceMemory != VK_NULL_HANDLE)
	{
		mpMappedMemory = nullptr;
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mHostVisibleVkBuffer, mHostVisibleVmaAllocation);
		mHostVisibleVkBuffer = VK_NULL_HANDLE;
		mHostVisibleVkDeviceMemory = VK_NULL_HANDLE;
		mHostVisibleVmaAllocation = VK_NULL_HANDLE;
	}

	if (mDeviceLocalVkDeviceMemory != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mDeviceLocalVkBuffer, mDeviceLocalVmaAllocation);
		mDeviceLocalVkBuffer = VK_NULL_HANDLE;
		mDeviceLocalVkDeviceMemory = VK_NULL_HANDLE;
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
	VkDeviceSize uiVerticesOffset = common::RoundUp(mInfo.iCount * iIndexSize, 4ll);
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
	vkCmdPipelineBarrier(vkCommandBuffer, stageFlags, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 1, &vkBufferMemoryBarrier, 0, nullptr);

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
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, stageFlags, 0, 0, 0, 1, &vkBufferMemoryBarrier, 0, nullptr);
}

} // namespace engine
