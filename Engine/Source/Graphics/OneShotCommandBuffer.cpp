#if defined(BT_CLIENT)

#include "OneShotCommandBuffer.h"

namespace engine
{

// Instances share gpDeviceManager->mOneShotVkCommandPool / mOneShotVkFence ("single-threaded, graphics queue
// only" — DeviceManager.cpp), so lifetimes must never overlap; the sbInUse atomic enforces that. The screenshot
// capture path constructs one in Graphics::RenderMainPresentAcquire on the main thread, between the UI submit and
// Present, with the mSubmitMain/mSubmitGlobal workers parked.
static std::atomic<bool> sbInUse = false;

OneShotCommandBuffer::OneShotCommandBuffer()
{
	ASSERT(!sbInUse.exchange(true));

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
	sbInUse.store(false);
}

void OneShotCommandBuffer::Execute()
{
	CHECK_VK(vkEndCommandBuffer(mVkCommandBuffer));

	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &gpDeviceManager->mOneShotVkFence));

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
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, gpDeviceManager->mOneShotVkFence));

	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &gpDeviceManager->mOneShotVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));
}

} // namespace engine

#endif // defined(BT_CLIENT)
