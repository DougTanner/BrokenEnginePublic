#include "OneShotCommandBuffer.h"

namespace engine
{

OneShotCommandBuffer::OneShotCommandBuffer()
{
	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = gpDeviceManager->mOneShotVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mVkCommandBuffer));
	VkName(VK_OBJECT_TYPE_COMMAND_BUFFER, mVkCommandBuffer, "OneShot");

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
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, gpDeviceManager->mOneShotVkCommandPool, 1, &mVkCommandBuffer);
}

void OneShotCommandBuffer::Execute(bool bWait)
{
	CHECK_VK(vkEndCommandBuffer(mVkCommandBuffer));

	if (bWait)
	{
		CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &gpDeviceManager->mOneShotVkFence));
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
		.pSignalSemaphores = nullptr,
	};
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, bWait ? gpDeviceManager->mOneShotVkFence : VK_NULL_HANDLE));

	if (bWait)
	{
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &gpDeviceManager->mOneShotVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));
	}
}

} // namespace engine
