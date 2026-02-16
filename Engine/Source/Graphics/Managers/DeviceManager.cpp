#include "DeviceManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "InstanceManager.h"
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
	bool bMaintenance9Available = false;
	for (const VkExtensionProperties& rExtension : availableExtensions)
	{
		if (strcmp(rExtension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			mbMemoryBudgetAvailable = true;
			Log("VK_EXT_memory_budget extension available");
		}
		else if (strcmp(rExtension.extensionName, VK_KHR_MAINTENANCE_9_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_KHR_MAINTENANCE_9_EXTENSION_NAME);
			bMaintenance9Available = true;
			Log("VK_KHR_maintenance9 extension available");
		}
	}

	VkPhysicalDeviceMaintenance9FeaturesKHR vkPhysicalDeviceMaintenance9FeaturesKHR =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_9_FEATURES_KHR,
		.pNext = nullptr,
		.maintenance9 = VK_TRUE,
	};
	VkPhysicalDeviceShaderClockFeaturesKHR vkPhysicalDeviceShaderClockFeaturesKHR =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR,
		.pNext = nullptr,
		.shaderSubgroupClock = VK_TRUE,
		.shaderDeviceClock = VK_TRUE,
	};

	// Build feature pNext chain tail: maintenance9 (if available) -> shader clock (if enabled)
	void* pFeatureChainTail = nullptr;
	if constexpr (kbEnableShaderRealtimeClock)
	{
		pFeatureChainTail = &vkPhysicalDeviceShaderClockFeaturesKHR;
	}
	if (bMaintenance9Available)
	{
		vkPhysicalDeviceMaintenance9FeaturesKHR.pNext = pFeatureChainTail;
		pFeatureChainTail = &vkPhysicalDeviceMaintenance9FeaturesKHR;
	}

	VkPhysicalDevice16BitStorageFeatures vkPhysicalDevice16BitStorageFeatures =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
		.pNext = pFeatureChainTail,
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
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
		.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
	};
	if constexpr (kbEnableGpuAssistedValidation || kbEnableDebugPrintf)
	{
		vkPhysicalDeviceVulkan12Features.storageBuffer8BitAccess = VK_TRUE;
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

	// Deduplicate queue family indices (Vulkan forbids duplicate family indices in VkDeviceCreateInfo)
	uint32_t pUniqueFamilyIndices[] {static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex), static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex), static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex)};
	std::sort(std::begin(pUniqueFamilyIndices), std::end(pUniqueFamilyIndices));
	uint32_t uiUniqueFamilyCount = static_cast<uint32_t>(std::unique(std::begin(pUniqueFamilyIndices), std::end(pUniqueFamilyIndices)) - std::begin(pUniqueFamilyIndices));

	VkDeviceQueueCreateInfo pVkDeviceQueueCreateInfo[3] {};
	for (uint32_t i = 0; i < uiUniqueFamilyCount; ++i)
	{
		pVkDeviceQueueCreateInfo[i] =
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = nullptr,
			.queueFamilyIndex = pUniqueFamilyIndices[i],
			.queueCount = 1,
			.pQueuePriorities = pfQueuePriorities,
		};
	}
	vkDeviceCreateInfo.queueCreateInfoCount = uiUniqueFamilyCount;
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
	if constexpr (kbEnableGpuAssistedValidation || kbEnableDebugPrintf)
	{
		vkPhysicalDeviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
		vkPhysicalDeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
		vkPhysicalDeviceFeatures.shaderInt64 = VK_TRUE;
	}
	vkDeviceCreateInfo.pEnabledFeatures = &vkPhysicalDeviceFeatures;
	CHECK_VK(vkCreateDevice(gpInstanceManager->mVkPhysicalDevice, &vkDeviceCreateInfo, nullptr, &mVkDevice));

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
		ASSERT(false);
		vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miPresentQueueFamilyIndex), 0, &mPresentVkQueue);
		VkName(VK_OBJECT_TYPE_QUEUE, mPresentVkQueue, "Present");
	}
	if (gpInstanceManager->miTransferQueueFamilyIndex == gpInstanceManager->miGraphicsQueueFamilyIndex)
	{
		mTransferVkQueue = mGraphicsVkQueue;
	}
	else
	{
		vkGetDeviceQueue(mVkDevice, static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex), 0, &mTransferVkQueue);
		VkName(VK_OBJECT_TYPE_QUEUE, mTransferVkQueue, "Transfer");
	}

	if (gpInstanceManager->miTransferQueueFamilyIndex == gpInstanceManager->miGraphicsQueueFamilyIndex)
	{
		Log("Transfer queue: shared with graphics queue (family {}), background GPU uploads disabled", gpInstanceManager->miGraphicsQueueFamilyIndex);
	}
	else
	{
		Log("Transfer queue: dedicated (family {}), background GPU uploads enabled", gpInstanceManager->miTransferQueueFamilyIndex);
	}

	// Query whether QFOT is optional for transfer -> graphics
	if (bMaintenance9Available && gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex)
	{
		uint32_t uiQueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2(gpInstanceManager->mVkPhysicalDevice, &uiQueueFamilyCount, nullptr);
		std::vector<VkQueueFamilyOwnershipTransferPropertiesKHR> qfotProperties(uiQueueFamilyCount, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_OWNERSHIP_TRANSFER_PROPERTIES_KHR, .pNext = nullptr});
		std::vector<VkQueueFamilyProperties2> queueFamilyProperties2(uiQueueFamilyCount, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2, .pNext = nullptr});
		for (uint32_t i = 0; i < uiQueueFamilyCount; ++i)
		{
			queueFamilyProperties2[i].pNext = &qfotProperties[i];
		}
		vkGetPhysicalDeviceQueueFamilyProperties2(gpInstanceManager->mVkPhysicalDevice, &uiQueueFamilyCount, queueFamilyProperties2.data());

		uint32_t uiTransferFamily = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex);
		uint32_t uiGraphicsFamily = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
		uint32_t uiOptimalMask = qfotProperties[uiTransferFamily].optimalImageTransferToQueueFamilies;
		mbTransferQfotOptional = (uiOptimalMask & (1u << uiGraphicsFamily)) != 0;
		Log("Transfer->Graphics QFOT optional: {} (transfer family {} optimal mask {:#010b}, graphics family {})", mbTransferQfotOptional, uiTransferFamily, uiOptimalMask, uiGraphicsFamily);
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
	CHECK_VK(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfo, nullptr, &mVkDescriptorPool));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, mVkDescriptorPool, "Global");

	// Descriptor pool for update-after-bind (dynamic pipelines only)
	VkDescriptorPoolSize pVkDescriptorPoolSizesUpdateAfterBind[]
	{
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2 * 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 8 * 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 4 * 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 128},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 64 * 1024},
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
	CHECK_VK(vkCreateDescriptorPool(gpDeviceManager->mVkDevice, &vkDescriptorPoolCreateInfoUpdateAfterBind, nullptr, &mVkDescriptorPoolUpdateAfterBind));
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

	CHECK_VK(vmaCreateAllocator(&allocatorCreateInfo, &mpAllocator));
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
