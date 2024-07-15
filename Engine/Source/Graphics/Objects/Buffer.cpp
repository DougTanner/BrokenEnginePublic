#include "Buffer.h"

#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"

namespace engine
{

using enum BufferFlags;

void Buffer::CreateBuffer([[maybe_unused]] std::string_view pcName, VkDeviceSize vkDeviceSize, VkBufferUsageFlags vkBufferUsageFlags, VkMemoryPropertyFlags vkMemoryPropertyFlags, VkBuffer& rVkBuffer, VkDeviceMemory& rVkDeviceMemory)
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
	CHECK_VK(vkCreateBuffer(gpDeviceManager->mVkDevice, &vkBufferCreateInfo, nullptr, &rVkBuffer));
	VK_NAME(VK_OBJECT_TYPE_BUFFER, rVkBuffer, pcName.data());

	VkMemoryRequirements vkMemoryRequirements {};
	vkGetBufferMemoryRequirements(gpDeviceManager->mVkDevice, rVkBuffer, &vkMemoryRequirements);
	VkMemoryAllocateInfo vkMemoryAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = nullptr,
		.allocationSize = vkMemoryRequirements.size,
		.memoryTypeIndex = 0,
	};
	int64_t iMemoryTypeIndex = FindMemoryType(vkMemoryRequirements.memoryTypeBits, vkMemoryPropertyFlags);
	vkMemoryAllocateInfo.memoryTypeIndex = static_cast<uint32_t>(iMemoryTypeIndex);
	CHECK_VK(vkAllocateMemory(gpDeviceManager->mVkDevice, &vkMemoryAllocateInfo, nullptr, &rVkDeviceMemory));

	CHECK_VK(vkBindBufferMemory(gpDeviceManager->mVkDevice, rVkBuffer, rVkDeviceMemory, 0));
}

void Buffer::RecordBarrier(VkCommandBuffer vkCommandBuffer, BufferBarrier eSource, BufferBarrier eDestination, VkBuffer vkBuffer)
{
	VkAccessFlags srcAccessMask = VK_ACCESS_NONE_KHR;
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	switch (eSource)
	{
		case BufferBarrier::kComputeWrite:
			srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		default:
			throw std::exception("Unhandled case in Buffer::RecordBarrier()");
	}

	VkAccessFlags dstAccessMask = VK_ACCESS_NONE_KHR;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	switch (eDestination)
	{
		case BufferBarrier::kComputeRead:
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case BufferBarrier::kShaderUniformRead:
			dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
			break;

		case BufferBarrier::kShaderIndirectRead:
			dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
			break;

		default:
			throw std::exception("Unhandled case in Buffer::RecordBarrier()");
	}

	VkBufferMemoryBarrier vkBufferMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = srcAccessMask,
		.dstAccessMask = dstAccessMask,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = vkBuffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 1, &vkBufferMemoryBarrier, 0, nullptr);
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
			if (mInfo.flags & kHostVisible)
			{
				Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVkDeviceMemory);
			}
			else
			{
				Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mHostVisibleVkBuffer, mHostVisibleVkDeviceMemory);
			}

			char* pMappedMemory = nullptr;
			VkDeviceSize roundedVkDeviceSize = common::RoundUp(mInfo.dataVkDeviceSize, gpInstanceManager->mVkPhysicalDeviceProperties.limits.nonCoherentAtomSize);
			CHECK_VK(vkMapMemory(gpDeviceManager->mVkDevice, mHostVisibleVkDeviceMemory, 0, roundedVkDeviceSize, 0, reinterpret_cast<void**>(&pMappedMemory)));
			mpMappedMemory = pMappedMemory;
		}

		if (mInfo.flags & kDeviceLocal || mInfo.flags & kCopyToDeviceLocalEveryFrame)
		{
			Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, vkBufferUsageFlagBits | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVkDeviceMemory);
		}
	}
	else
	{
		ASSERT(mInfo.flags & kIndexVertex);
		Buffer::CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDeviceLocalVkBuffer, mDeviceLocalVkDeviceMemory);
	}

	if (mInfo.flags & kDeviceLocal)
	{
		// Copy to host visible buffer
		VkBuffer vkBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vkDeviceMemory = VK_NULL_HANDLE;
		CreateBuffer(mInfo.pcName, mInfo.dataVkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vkBuffer, vkDeviceMemory);
		void* pHostVisibleMappedMemory = nullptr;
		VkDeviceSize roundedVkDeviceSize = common::RoundUp(mInfo.dataVkDeviceSize, gpInstanceManager->mVkPhysicalDeviceProperties.limits.nonCoherentAtomSize);
		CHECK_VK(vkMapMemory(gpDeviceManager->mVkDevice, vkDeviceMemory, 0, roundedVkDeviceSize, 0, &pHostVisibleMappedMemory));
		dataFunction(pHostVisibleMappedMemory);
		vkUnmapMemory(gpDeviceManager->mVkDevice, vkDeviceMemory);

		// Copy to device local memory
		OneShotCommandBuffer oneShotCommandBuffer;
		VkBufferCopy vkBufferCopy
		{
			.srcOffset = 0,
			.dstOffset = 0,
			.size = roundedVkDeviceSize,
		};
		vkCmdCopyBuffer(oneShotCommandBuffer.mVkCommandBuffer, vkBuffer, mDeviceLocalVkBuffer, 1, &vkBufferCopy);
		oneShotCommandBuffer.Execute(true);

		// Clean up
		vkDestroyBuffer(gpDeviceManager->mVkDevice, vkBuffer, nullptr);
		vkFreeMemory(gpDeviceManager->mVkDevice, vkDeviceMemory, nullptr);
	}
}

void Buffer::Destroy() noexcept
{
	if (mHostVisibleVkDeviceMemory != VK_NULL_HANDLE || mDeviceLocalVkDeviceMemory != VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(gpDeviceManager->mVkDevice);
	}
	
	if (mHostVisibleVkDeviceMemory != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(gpDeviceManager->mVkDevice, mHostVisibleVkBuffer, nullptr);
		mHostVisibleVkBuffer = VK_NULL_HANDLE;
		mpMappedMemory = nullptr;
		vkFreeMemory(gpDeviceManager->mVkDevice, mHostVisibleVkDeviceMemory, nullptr);
		mHostVisibleVkDeviceMemory = VK_NULL_HANDLE;
	}

	if (mDeviceLocalVkDeviceMemory != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(gpDeviceManager->mVkDevice, mDeviceLocalVkBuffer, nullptr);
		mDeviceLocalVkBuffer = VK_NULL_HANDLE;
		vkFreeMemory(gpDeviceManager->mVkDevice, mDeviceLocalVkDeviceMemory, nullptr);
		mDeviceLocalVkDeviceMemory = VK_NULL_HANDLE;
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

void Buffer::RecordCopy(VkCommandBuffer vkCommandBuffer)
{
	ASSERT((mInfo.flags & kUniform || mInfo.flags & kStorage) && mInfo.flags & kCopyToDeviceLocalEveryFrame);
	
	VkDeviceSize roundedVkDeviceSize = common::RoundUp(mInfo.dataVkDeviceSize, gpInstanceManager->mVkPhysicalDeviceProperties.limits.nonCoherentAtomSize);

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
		.size = roundedVkDeviceSize,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 1, &vkBufferMemoryBarrier, 0, nullptr);

	VkBufferCopy vkBufferCopy
	{
		.srcOffset = 0,
		.dstOffset = 0,
		.size = roundedVkDeviceSize,
	};
	vkCmdCopyBuffer(vkCommandBuffer, mHostVisibleVkBuffer, mDeviceLocalVkBuffer, 1, &vkBufferCopy);

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
		.size = roundedVkDeviceSize,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 1, &vkBufferMemoryBarrier, 0, nullptr);
}

} // namespace engine
