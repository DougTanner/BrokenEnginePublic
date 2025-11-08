#include "DeviceManager.h"

#pragma warning(push, 0)
#pragma warning(disable : 4100 6326 6386 6387)
#define VMA_IMPLEMENTATION
// Define VMA Vulkan version to match runtime configuration
#if defined(ENABLE_VULKAN_1_2)
	#define VMA_VULKAN_VERSION 1002000 // 1.2.0
#else
	#define VMA_VULKAN_VERSION 1001000 // 1.1.0
#endif
#include <vma/vk_mem_alloc.h>
#pragma warning(pop)

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
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
	VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
#endif
};

DeviceManager::DeviceManager()
{
	gpDeviceManager = this;

	SCOPED_BOOT_TIMER(kBootTimerDeviceManager);

	// Query available device extensions
	uint32_t uiExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(gpInstanceManager->mVkPhysicalDevice, nullptr, &uiExtensionCount, nullptr);
	std::vector<VkExtensionProperties> availableExtensions(uiExtensionCount);
	vkEnumerateDeviceExtensionProperties(gpInstanceManager->mVkPhysicalDevice, nullptr, &uiExtensionCount, availableExtensions.data());

	// Build device extension list with optional VK_EXT_memory_budget
	std::vector<const char*> deviceExtensions;
	for (const char* pcExtension : kpcDeviceExtensionNames)
	{
		deviceExtensions.push_back(pcExtension);
	}
	for (const auto& extension : availableExtensions)
	{
		if (strcmp(extension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			mbMemoryBudgetAvailable = true;
			LOG("VK_EXT_memory_budget extension available");
			break;
		}
	}

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
	vkDeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	vkDeviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures
	{
		.sampleRateShading = VK_TRUE,
	#if defined(ENABLE_WIREFRAME)
		.fillModeNonSolid = VK_TRUE,
	#endif
		.samplerAnisotropy = VK_TRUE,
		.textureCompressionBC = VK_TRUE,
	#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
		.shaderInt64 = VK_TRUE,
	#endif
	#if !defined(ENABLE_32_BIT_BOOL)
		.shaderInt16 = VK_TRUE,
	#endif
	};
	vkDeviceCreateInfo.pEnabledFeatures = &vkPhysicalDeviceFeatures;
	CHECK_VK(vkCreateDevice(gpInstanceManager->mVkPhysicalDevice, &vkDeviceCreateInfo, nullptr, &mVkDevice));

	// Load device-specific Vulkan functions via Volk
	volkLoadDevice(mVkDevice);

	VK_NAME(VK_OBJECT_TYPE_DEVICE, mVkDevice, "Logical");

	// Retrieve the queues now that the device has been created
	vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex), 0, &mGraphicsVkQueue);
	VK_NAME(VK_OBJECT_TYPE_QUEUE, mGraphicsVkQueue, "Graphics");
	if (gpInstanceManager->miGraphicsQueueFamilyIndex == gpInstanceManager->miPresentQueueFamilyIndex)
	{
		mPresentVkQueue = mGraphicsVkQueue;
	}
	else
	{
		ASSERT(false);
		vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex), 0, &mPresentVkQueue);
		VK_NAME(VK_OBJECT_TYPE_QUEUE, mPresentVkQueue, "Present");
	}

	// Descriptor pool
	VkDescriptorPoolSize pVkDescriptorPoolSizes[]
	{
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1024 },
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4 * 1024 },
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 512 },
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 32 },
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 4 * 1024 },
		VkDescriptorPoolSize { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 16 },
	};
	VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.maxSets = 0,
		.poolSizeCount = static_cast<uint32_t>(std::size(pVkDescriptorPoolSizes)),
		.pPoolSizes = pVkDescriptorPoolSizes,
	};
	for (const VkDescriptorPoolSize& rVkDescriptorPoolSize : pVkDescriptorPoolSizes)
	{
		vkDescriptorPoolCreateInfo.maxSets += rVkDescriptorPoolSize.descriptorCount;
	}
	CHECK_VK(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfo, nullptr, &mVkDescriptorPool));
	VK_NAME(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mVkDescriptorPool, "Global");

	// Initialize VMA
	mVmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
	mVmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo allocatorCreateInfo =
	{
		.flags = mbMemoryBudgetAvailable ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : static_cast<VmaAllocatorCreateFlags>(0),
		.physicalDevice = gpInstanceManager->mVkPhysicalDevice,
		.device = mVkDevice,
		.pVulkanFunctions = &mVmaFunctions,
		.instance = gpInstanceManager->mVkInstance,
	#if defined(ENABLE_VULKAN_1_2)
		.vulkanApiVersion = VK_API_VERSION_1_2,
	#else
		.vulkanApiVersion = VK_API_VERSION_1_1,
	#endif
	};

	CHECK_VK(vmaCreateAllocator(&allocatorCreateInfo, &mpAllocator));
}

DeviceManager::~DeviceManager()
{
	vmaDestroyAllocator(mpAllocator);
	mpAllocator = nullptr;

	// No need to free the individual descriptor sets: "When a pool is destroyed, all descriptor sets allocated from the pool are implicitly freed and become invalid"
	vkDestroyDescriptorPool(gpDeviceManager->mVkDevice, mVkDescriptorPool, nullptr);

	vkDestroyDevice(mVkDevice, nullptr);

	gpDeviceManager = nullptr;
}

} // namespace engine
