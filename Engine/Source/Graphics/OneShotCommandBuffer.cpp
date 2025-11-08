#include "OneShotCommandBuffer.h"

#include "Graphics/Graphics.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/InstanceManager.h"

namespace engine
{

OneShotCommandBuffer::OneShotCommandBuffer()
{
	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mVkCommandPool));
	VK_NAME(VK_OBJECT_TYPE_COMMAND_POOL, mVkCommandPool, "OneShot");

	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mVkCommandBuffer));
	VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mVkCommandBuffer, "OneShot");

	VkFenceCreateInfo vkFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mVkFence));
	VK_NAME(VK_OBJECT_TYPE_FENCE, mVkFence, "OneShot");

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};

	CHECK_VK(vkBeginCommandBuffer(mVkCommandBuffer, &vkCommandBufferBeginInfo));
}

OneShotCommandBuffer::~OneShotCommandBuffer()
{
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mVkCommandPool, 1, &mVkCommandBuffer);
	vkDestroyFence(gpDeviceManager->mVkDevice, mVkFence, nullptr);
	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mVkCommandPool, nullptr);
}

void OneShotCommandBuffer::Execute(bool bWait)
{
	CHECK_VK(vkEndCommandBuffer(mVkCommandBuffer));

	if (bWait)
	{
		CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &mVkFence));
	}

	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 0,
		.pWaitSemaphores = nullptr,
		.pWaitDstStageMask = nullptr,
		.commandBufferCount = 1,
		.pCommandBuffers = &mVkCommandBuffer,
		.signalSemaphoreCount = 0,
	};
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, bWait ? mVkFence : VK_NULL_HANDLE));

	if (bWait)
	{
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &mVkFence, VK_TRUE, kFenceTimeoutNs.count()));
	}
}

} // namespace engine
