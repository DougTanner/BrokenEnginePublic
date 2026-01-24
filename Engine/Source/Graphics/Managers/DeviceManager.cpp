#include "DeviceManager.h"

#include <vma/vk_mem_alloc.h>

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

DeviceManager::DeviceManager()
{
	gpDeviceManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerDeviceManager);

	// Query available device extensions
	uint32_t uiExtensionCount = 0;
	vkEnumerateDeviceExtensionProperties(gpInstanceManager->mVkPhysicalDevice, nullptr, &uiExtensionCount, nullptr);
	std::vector<VkExtensionProperties> availableExtensions(uiExtensionCount);
	vkEnumerateDeviceExtensionProperties(gpInstanceManager->mVkPhysicalDevice, nullptr, &uiExtensionCount, availableExtensions.data());

	// Build device extension list
	std::vector<const char*> deviceExtensions;
	deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	if constexpr (kbEnableDebugPrintf)
	{
		deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
	}
	if constexpr (kbEnableShaderRealtimeClock)
	{
		deviceExtensions.push_back(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
	}
	for (const VkExtensionProperties& rExtension : availableExtensions)
	{
		if (strcmp(rExtension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			mbMemoryBudgetAvailable = true;
			Log("VK_EXT_memory_budget extension available");
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
		.pNext = kbEnableShaderRealtimeClock ? &vkPhysicalDeviceShaderClockFeaturesKHR : nullptr,
		.storageBuffer16BitAccess = VK_TRUE,
		.uniformAndStorageBuffer16BitAccess = VK_TRUE,
		.storagePushConstant16 = VK_FALSE,
		.storageInputOutput16 = VK_FALSE,
	};
	VkPhysicalDeviceVulkan12Features vkPhysicalDeviceVulkan12Features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &vkPhysicalDevice16BitStorageFeatures,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
	};
	if constexpr (kbEnableGpuAssistedValidation)
	{
		vkPhysicalDeviceVulkan12Features.storageBuffer8BitAccess = VK_TRUE;
		vkPhysicalDeviceVulkan12Features.scalarBlockLayout = VK_TRUE;
		vkPhysicalDeviceVulkan12Features.timelineSemaphore = VK_TRUE;
		vkPhysicalDeviceVulkan12Features.bufferDeviceAddress = VK_TRUE;
		vkPhysicalDeviceVulkan12Features.vulkanMemoryModel = VK_TRUE;
		vkPhysicalDeviceVulkan12Features.vulkanMemoryModelDeviceScope = VK_TRUE;
	}
	VkDeviceCreateInfo vkDeviceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &vkPhysicalDeviceVulkan12Features,
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
	vkDeviceCreateInfo.enabledLayerCount = kbEnableVulkanDebugLayers ? static_cast<uint32_t>(gpInstanceManager->mValidationLayers.size()) : 0;
	vkDeviceCreateInfo.ppEnabledLayerNames = kbEnableVulkanDebugLayers ? gpInstanceManager->mValidationLayers.data() : nullptr;
	vkDeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	vkDeviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures
	{
		.sampleRateShading = VK_TRUE,
		.samplerAnisotropy = VK_TRUE,
		.textureCompressionBC = VK_TRUE,
		.shaderInt64 = kbEnableShaderRealtimeClock ? VK_TRUE : VK_FALSE,
	#if !defined(ENABLE_32_BIT_BOOL)
		.shaderInt16 = VK_TRUE,
	#endif
	};
	if constexpr (kbEnableWireframe)
	{
		vkPhysicalDeviceFeatures.fillModeNonSolid = VK_TRUE;
	}
	if constexpr (kbEnableGpuAssistedValidation)
	{
		vkPhysicalDeviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
		vkPhysicalDeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
		vkPhysicalDeviceFeatures.shaderInt64 = VK_TRUE;
	}
	vkDeviceCreateInfo.pEnabledFeatures = &vkPhysicalDeviceFeatures;
	CheckVk(vkCreateDevice(gpInstanceManager->mVkPhysicalDevice, &vkDeviceCreateInfo, nullptr, &mVkDevice));

	// Load device-specific Vulkan functions via Volk
	volkLoadDevice(mVkDevice);

	VkName(VK_OBJECT_TYPE_DEVICE, mVkDevice, "Logical");

	// Retrieve the queues now that the device has been created
	vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex), 0, &mGraphicsVkQueue);
	VkName(VK_OBJECT_TYPE_QUEUE, mGraphicsVkQueue, "Graphics");
	if (gpInstanceManager->miGraphicsQueueFamilyIndex == gpInstanceManager->miPresentQueueFamilyIndex)
	{
		mPresentVkQueue = mGraphicsVkQueue;
	}
	else
	{
		Assert(false);
		vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex), 0, &mPresentVkQueue);
		VkName(VK_OBJECT_TYPE_QUEUE, mPresentVkQueue, "Present");
	}

	// Descriptor pool
	VkDescriptorPoolSize pVkDescriptorPoolSizes[]
	{
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 512},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2 * 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 128},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 32},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 32},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 32},
	};
	VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = 0,
		.poolSizeCount = static_cast<uint32_t>(std::size(pVkDescriptorPoolSizes)),
		.pPoolSizes = pVkDescriptorPoolSizes,
	};
	for (const VkDescriptorPoolSize& rVkDescriptorPoolSize : pVkDescriptorPoolSizes)
	{
		vkDescriptorPoolCreateInfo.maxSets += rVkDescriptorPoolSize.descriptorCount;
	}
	CheckVk(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfo, nullptr, &mVkDescriptorPool));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mVkDescriptorPool, "Global");

	// Descriptor pool for update-after-bind (dynamic pipelines only)
	VkDescriptorPoolSize pVkDescriptorPoolSizesUpdateAfterBind[]
	{
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 128},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 256},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 128},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 32},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 2 * 1024},
	};
	VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfoUpdateAfterBind
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = 0,
		.poolSizeCount = static_cast<uint32_t>(std::size(pVkDescriptorPoolSizesUpdateAfterBind)),
		.pPoolSizes = pVkDescriptorPoolSizesUpdateAfterBind,
	};
	for (const VkDescriptorPoolSize& rVkDescriptorPoolSize : pVkDescriptorPoolSizesUpdateAfterBind)
	{
		vkDescriptorPoolCreateInfoUpdateAfterBind.maxSets += rVkDescriptorPoolSize.descriptorCount;
	}
	CheckVk(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfoUpdateAfterBind, nullptr, &mVkDescriptorPoolUpdateAfterBind));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mVkDescriptorPoolUpdateAfterBind, "UpdateAfterBind");

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
		.vulkanApiVersion = VK_API_VERSION_1_2,
	};

	CheckVk(vmaCreateAllocator(&allocatorCreateInfo, &mpAllocator));
}

DeviceManager::~DeviceManager()
{
	vmaDestroyAllocator(mpAllocator);
	mpAllocator = nullptr;

	// All descriptor sets freed explicitly in Pipeline::Destroy() before reaching here
	vkDestroyDescriptorPool(gpDeviceManager->mVkDevice, mVkDescriptorPoolUpdateAfterBind, nullptr);
	vkDestroyDescriptorPool(gpDeviceManager->mVkDevice, mVkDescriptorPool, nullptr);

	vkDestroyDevice(mVkDevice, nullptr);

	gpDeviceManager = nullptr;
}

} // namespace engine
