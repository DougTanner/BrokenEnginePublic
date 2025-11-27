#include "CommandBuffers.h"

#include "Graphics/Graphics.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/InstanceManager.h"

namespace engine
{

CommandBuffers::CommandBuffers(int64_t iFramebuffer)
: miFramebuffer(iFramebuffer)
{
	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mCommandPool));
	VK_NAME(VK_OBJECT_TYPE_COMMAND_POOL, mCommandPool, std::format("_{}", iFramebuffer).c_str());

	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mGlobalCommandBuffer));
	VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mGlobalCommandBuffer, std::format("Global_{}", iFramebuffer).c_str());
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mImageCommandBuffer));
	VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mImageCommandBuffer, std::format("Image_{}", iFramebuffer).c_str());

	VkSemaphoreCreateInfo vkSemaphoreCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
	};
	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mGlobalFinishedVkSemaphore));
	VK_NAME(VK_OBJECT_TYPE_SEMAPHORE, mGlobalFinishedVkSemaphore, std::format("GlobalFinished_{}", iFramebuffer).c_str());
	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mImageFinishedVkSemaphore));
	VK_NAME(VK_OBJECT_TYPE_SEMAPHORE, mImageFinishedVkSemaphore, std::format("ImageFinished_{}", iFramebuffer).c_str());

	VkFenceCreateInfo vkFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mVkFence));
	VK_NAME(VK_OBJECT_TYPE_FENCE, mVkFence, std::format("Global_{}", iFramebuffer).c_str());
}

CommandBuffers::~CommandBuffers()
{
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mCommandPool, 1, &mGlobalCommandBuffer);
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mCommandPool, 1, &mImageCommandBuffer);
	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mCommandPool, nullptr);

	vkDestroySemaphore(gpDeviceManager->mVkDevice, mGlobalFinishedVkSemaphore, nullptr);
	vkDestroySemaphore(gpDeviceManager->mVkDevice, mImageFinishedVkSemaphore, nullptr);

	vkDestroyFence(gpDeviceManager->mVkDevice, mVkFence, nullptr);
}

} // namespace engine
