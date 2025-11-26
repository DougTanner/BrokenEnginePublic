#include "CommandBuffers.h"

#include "Graphics/Graphics.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/InstanceManager.h"

namespace engine
{

CommandBuffers::CommandBuffers()
{
	for (int64_t i = 0; i < kiCommandBuffersPerFramebuffer; ++i)
	{
		VkCommandPoolCreateInfo vkCommandPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
		};
		CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mpCommandPools[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_POOL, mpCommandPools[i], std::format("{}", i).c_str());

		VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.pNext = nullptr,
			.commandPool = mpCommandPools[i],
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mpGlobalCommandBuffers[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpGlobalCommandBuffers[i], std::format("Global{}", i).c_str());
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, &mpImageCommandBuffers[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpImageCommandBuffers[i], std::format("Image{}", i).c_str());

		// Allocate secondary command buffers - one per glTF pipeline type + one per lighting pipeline + one grouped for all non-glTF/non-lighting
		// DT: TEMP Remove all this, once all are on dynamic pipelines
		VkCommandBufferAllocateInfo vkSecondaryCommandBufferAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.pNext = nullptr,
			.commandPool = mpCommandPools[i],
			.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
			.commandBufferCount = 1,
		};
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkSecondaryCommandBufferAllocateInfo, &mpPointLightsSecondaryBuffer[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpPointLightsSecondaryBuffer[i], std::format("PointLightsSecondary{}", i).c_str());
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkSecondaryCommandBufferAllocateInfo, &mpHexShieldsLightingSecondaryBuffer[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpHexShieldsLightingSecondaryBuffer[i], std::format("HexShieldsLightingSecondary{}", i).c_str());
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkSecondaryCommandBufferAllocateInfo, &mpLongParticlesLightingSecondaryBuffer[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpLongParticlesLightingSecondaryBuffer[i], std::format("LongParticlesLightingSecondary{}", i).c_str());
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkSecondaryCommandBufferAllocateInfo, &mpSquareParticlesLightingSecondaryBuffer[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpSquareParticlesLightingSecondaryBuffer[i], std::format("SquareParticlesLightingSecondary{}", i).c_str());
		CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkSecondaryCommandBufferAllocateInfo, &mpSceneSecondaryBuffer[i]));
		VK_NAME(VK_OBJECT_TYPE_COMMAND_BUFFER, mpSceneSecondaryBuffer[i], std::format("SceneSecondary{}", i).c_str());

		VkSemaphoreCreateInfo globalFinishedVkSemaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
		};
		CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &globalFinishedVkSemaphoreCreateInfo, nullptr, &mpGlobalFinishedVkSemaphores[i]));
		VK_NAME(VK_OBJECT_TYPE_SEMAPHORE, mpGlobalFinishedVkSemaphores[i], std::format("GlobalFinished {}", i).c_str());

		VkSemaphoreCreateInfo imageFinishedVkSemaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
		};
		CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &imageFinishedVkSemaphoreCreateInfo, nullptr, &mpImageFinishedVkSemaphores[i]));
		VK_NAME(VK_OBJECT_TYPE_SEMAPHORE, mpImageFinishedVkSemaphores[i], std::format("ImageFinished {}", i).c_str());

		VkFenceCreateInfo globalFinishedVkFenceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = VK_FENCE_CREATE_SIGNALED_BIT,
		};
		CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &globalFinishedVkFenceCreateInfo, nullptr, &mpVkFences[i]));
		VK_NAME(VK_OBJECT_TYPE_FENCE, mpVkFences[i], std::format("Global {}", i).c_str());
	}
}

CommandBuffers::~CommandBuffers()
{
	for (int64_t i = 0; i < kiCommandBuffersPerFramebuffer; ++i)
	{
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpGlobalCommandBuffers[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpImageCommandBuffers[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpPointLightsSecondaryBuffer[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpHexShieldsLightingSecondaryBuffer[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpLongParticlesLightingSecondaryBuffer[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpSquareParticlesLightingSecondaryBuffer[i]);
		vkFreeCommandBuffers(gpDeviceManager->mVkDevice, mpCommandPools[i], 1, &mpSceneSecondaryBuffer[i]);
		vkDestroyCommandPool(gpDeviceManager->mVkDevice, mpCommandPools[i], nullptr);

		vkDestroySemaphore(gpDeviceManager->mVkDevice, mpGlobalFinishedVkSemaphores[i], nullptr);
		vkDestroySemaphore(gpDeviceManager->mVkDevice, mpImageFinishedVkSemaphores[i], nullptr);

		vkDestroyFence(gpDeviceManager->mVkDevice, mpVkFences[i], nullptr);
	}
}

} // namespace engine
