#include "CommandBuffers.h"

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
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mVkCommandPool));
	VkName(VK_OBJECT_TYPE_COMMAND_POOL, mVkCommandPool, std::format("_{}", iFramebuffer).c_str());

	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mGlobalVkCommandBuffer));
	VkName(VK_OBJECT_TYPE_COMMAND_BUFFER, mGlobalVkCommandBuffer, std::format("Global_{}", iFramebuffer).c_str());
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mMainVkCommandBuffer));
	VkName(VK_OBJECT_TYPE_COMMAND_BUFFER, mMainVkCommandBuffer, std::format("Main_{}", iFramebuffer).c_str());

	VkSemaphoreCreateInfo vkSemaphoreCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
	};
	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mGlobalFinishedVkSemaphore));
	VkName(VK_OBJECT_TYPE_SEMAPHORE, mGlobalFinishedVkSemaphore, std::format("GlobalFinished_{}", iFramebuffer).c_str());
	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mMainFinishedVkSemaphore));
	VkName(VK_OBJECT_TYPE_SEMAPHORE, mMainFinishedVkSemaphore, std::format("MainFinished_{}", iFramebuffer).c_str());

	VkFenceCreateInfo vkFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &mVkFence));
	VkName(VK_OBJECT_TYPE_FENCE, mVkFence, std::format("Global_{}", iFramebuffer).c_str());

	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mImGuiVkCommandBuffer));
	VkName(VK_OBJECT_TYPE_COMMAND_BUFFER, mImGuiVkCommandBuffer, std::format("ImGui_{}", iFramebuffer).c_str());

	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mImGuiFinishedVkSemaphore));
	VkName(VK_OBJECT_TYPE_SEMAPHORE, mImGuiFinishedVkSemaphore, std::format("ImGuiFinished_{}", iFramebuffer).c_str());
}

CommandBuffers::~CommandBuffers()
{
	vkDestroySemaphore(gpDeviceManager->mVkDevice, mImGuiFinishedVkSemaphore, nullptr);

	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mVkCommandPool, 1, &mGlobalVkCommandBuffer);
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mVkCommandPool, 1, &mMainVkCommandBuffer);
	vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mVkCommandPool, 1, &mImGuiVkCommandBuffer);
	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mVkCommandPool, nullptr);

	vkDestroySemaphore(gpDeviceManager->mVkDevice, mGlobalFinishedVkSemaphore, nullptr);
	vkDestroySemaphore(gpDeviceManager->mVkDevice, mMainFinishedVkSemaphore, nullptr);

	vkDestroyFence(gpDeviceManager->mVkDevice, mVkFence, nullptr);
}

} // namespace engine
