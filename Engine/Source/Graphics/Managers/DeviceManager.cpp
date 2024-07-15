#include "DeviceManager.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Shaders/ShaderLayouts.h"

namespace engine
{

constexpr const char* kpcDeviceExtensionNames[]
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if defined(ENABLE_DEBUG_PRINTF_EXT)
	VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
#endif
};

int64_t FindMemoryType(int64_t iTypeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties vkPhysicalDeviceMemoryProperties {};
	vkGetPhysicalDeviceMemoryProperties(gpInstanceManager->mVkPhysicalDevice, &vkPhysicalDeviceMemoryProperties);

	for (int64_t i = 0; i < vkPhysicalDeviceMemoryProperties.memoryTypeCount; ++i)
	{
		if ((iTypeFilter & (1ll << i)) != 0 && (vkPhysicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	// NOTE: We are assuming when VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT is true, so is VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
	//       This should hold true except in some weird cases on Android with ARM GPUs
	//       If this ever becomes a problem, we would need to check !bCoherant -> vkFlushMappedMemoryRanges
	DEBUG_BREAK();
	return 0;
}

DeviceManager::DeviceManager()
{
	gpDeviceManager = this;

	SCOPED_BOOT_TIMER(kBootTimerDeviceManager);

	VkPhysicalDeviceShaderClockFeaturesKHR vkPhysicalDeviceShaderClockFeaturesKHR =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR,
		.pNext = nullptr,
		.shaderSubgroupClock = VK_TRUE,
		.shaderDeviceClock = VK_TRUE,
	};
	VkPhysicalDevice16BitStorageFeatures vkPhysicalDevice16BitStorageFeatures =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
	#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
		.pNext = &vkPhysicalDeviceShaderClockFeaturesKHR,
	#else
		.pNext = nullptr,
	#endif
		.storageBuffer16BitAccess = VK_TRUE,
		.uniformAndStorageBuffer16BitAccess = VK_TRUE,
		.storagePushConstant16 = VK_FALSE,
		.storageInputOutput16 = VK_FALSE,
	};
#if defined(ENABLE_VULKAN_1_2)
	VkPhysicalDeviceVulkan12Features vkPhysicalDeviceVulkan12Features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &vkPhysicalDevice16BitStorageFeatures,
#if defined(ENABLE_VULKAN_8BIT)
		.storageBuffer8BitAccess = VK_TRUE,
		.shaderInt8 = VK_TRUE,
#endif
	};
#endif
	VkDeviceCreateInfo vkDeviceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
	#if defined(ENABLE_VULKAN_1_2)
		.pNext = &vkPhysicalDeviceVulkan12Features,
	#else
		.pNext = &vkPhysicalDevice16BitStorageFeatures,
	#endif
		.flags = 0,
	};
	float pfQueuePriorities[] {1.0f};
	VkDeviceQueueCreateInfo pVkDeviceQueueCreateInfo[]
	{
		VkDeviceQueueCreateInfo {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .pNext = nullptr},
		VkDeviceQueueCreateInfo {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .pNext = nullptr},
	};
	if (gpInstanceManager->miGraphicsQueueFamilyIndex == gpInstanceManager->miPresentQueueFamilyIndex)
	{
		vkDeviceCreateInfo.queueCreateInfoCount = 1;

		pVkDeviceQueueCreateInfo[0].queueCount = 1;
		pVkDeviceQueueCreateInfo[0].pQueuePriorities = pfQueuePriorities;
		pVkDeviceQueueCreateInfo[0].queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
	}
	else
	{
		vkDeviceCreateInfo.queueCreateInfoCount = 2;

		pVkDeviceQueueCreateInfo[0].queueCount = 1;
		pVkDeviceQueueCreateInfo[0].pQueuePriorities = pfQueuePriorities;
		pVkDeviceQueueCreateInfo[0].queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);

		pVkDeviceQueueCreateInfo[1].queueCount = 1;
		pVkDeviceQueueCreateInfo[1].pQueuePriorities = pfQueuePriorities;
		pVkDeviceQueueCreateInfo[1].queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex);
	}
	vkDeviceCreateInfo.pQueueCreateInfos = pVkDeviceQueueCreateInfo;
#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	vkDeviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(gpInstanceManager->mValidationLayers.size());
	vkDeviceCreateInfo.ppEnabledLayerNames = gpInstanceManager->mValidationLayers.data();
#endif
	vkDeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(kpcDeviceExtensionNames));
	vkDeviceCreateInfo.ppEnabledExtensionNames = kpcDeviceExtensionNames;
	VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures
	{
		.sampleRateShading = VK_TRUE,
	#if defined(ENABLE_WIREFRAME)
		.fillModeNonSolid = VK_TRUE,
	#endif
		.samplerAnisotropy = VK_TRUE,
	#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
		.shaderInt64 = VK_TRUE,
	#endif
	#if !defined(ENABLE_32_BIT_BOOL)
		.shaderInt16 = VK_TRUE,
	#endif
	};
	vkDeviceCreateInfo.pEnabledFeatures = &vkPhysicalDeviceFeatures;
	CHECK_VK(vkCreateDevice(gpInstanceManager->mVkPhysicalDevice, &vkDeviceCreateInfo, nullptr, &mVkDevice));

	// Retrieve the queues now that the device has been created
	vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex), 0, &mGraphicsVkQueue);
	if (gpInstanceManager->miGraphicsQueueFamilyIndex == gpInstanceManager->miPresentQueueFamilyIndex)
	{
		mPresentVkQueue = mGraphicsVkQueue;
	}
	else
	{
		ASSERT(false);
		vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex), 0, &mPresentVkQueue);
	}

	// Descriptor pool
	int64_t iMaxSets = static_cast<int64_t>(4 * kPipelineCount + 4 * shaders::kiMaxLightingBlurCount) + 1;
	int64_t iMaxFramebuffers = 4;
	int64_t iUniformBuffers = 2;
	int64_t iImageSamplers = 8;
	int64_t iStorageBuffers = 1;
	int64_t iSamplers = 1;
	int64_t iSampledImages = 1;
	int64_t iStorageImages = 1;
	VkDescriptorPoolSize pVkDescriptorPoolSizes[]
	{
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = static_cast<uint32_t>(iUniformBuffers * iMaxSets * iMaxFramebuffers),
		},
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = static_cast<uint32_t>(iImageSamplers * iMaxSets * iMaxFramebuffers),
		},
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = static_cast<uint32_t>(iStorageBuffers * iMaxSets * iMaxFramebuffers),
		},
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLER,
			.descriptorCount = static_cast<uint32_t>(iSamplers * iMaxSets * iMaxFramebuffers),
		},
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.descriptorCount = static_cast<uint32_t>(iSampledImages * iMaxSets * iMaxFramebuffers),
		},
		VkDescriptorPoolSize
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = static_cast<uint32_t>(iStorageImages * iMaxSets * iMaxFramebuffers),
		},
	};
	VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = static_cast<uint32_t>((iUniformBuffers + iImageSamplers + iStorageBuffers) * iMaxSets * iMaxFramebuffers),
		.poolSizeCount = static_cast<uint32_t>(std::size(pVkDescriptorPoolSizes)),
		.pPoolSizes = pVkDescriptorPoolSizes,
	};
	CHECK_VK(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfo, nullptr, &mVkDescriptorPool));
}

DeviceManager::~DeviceManager()
{
	// No need to free the individual descriptor sets: "When a pool is destroyed, all descriptor sets allocated from the pool are implicitly freed and become invalid"
	vkDestroyDescriptorPool(gpDeviceManager->mVkDevice, mVkDescriptorPool, nullptr);

	vkDestroyDevice(mVkDevice, nullptr);

	gpDeviceManager = nullptr;
}

} // namespace engine
