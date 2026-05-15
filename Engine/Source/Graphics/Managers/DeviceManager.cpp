#include "DeviceManager.h"

#include "Profile/ProfileManager.h"

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
	if constexpr (kbDebugPrintf)
	{
		deviceExtensions.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
	}
	if constexpr (kbShaderRealtimeClock)
	{
		deviceExtensions.push_back(VK_KHR_SHADER_CLOCK_EXTENSION_NAME);
	}
	bool bMaintenance9Available = false;
	bool bLineRasterizationAvailable = false;
	for (const VkExtensionProperties& rExtension : availableExtensions)
	{
		if (strcmp(rExtension.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
			mbMemoryBudgetAvailable = true;
			LOG(kGraphics, kInfo, "VK_EXT_memory_budget extension available");
		}
		else if (strcmp(rExtension.extensionName, VK_KHR_MAINTENANCE_9_EXTENSION_NAME) == 0)
		{
			deviceExtensions.push_back(VK_KHR_MAINTENANCE_9_EXTENSION_NAME);
			bMaintenance9Available = true;
			LOG(kGraphics, kInfo, "VK_KHR_maintenance9 extension available");
		}
		else if (strcmp(rExtension.extensionName, VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME) == 0)
		{
			bLineRasterizationAvailable = true;
			LOG(kGraphics, kInfo, "VK_EXT_line_rasterization extension available");
		}
	}

	// Probe for smoothLines / rectangularLines support before deciding to enable the extension
	VkPhysicalDeviceLineRasterizationFeaturesEXT vkPhysicalDeviceLineRasterizationFeaturesEXT =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT,
		.pNext = nullptr,
	};
	if (bLineRasterizationAvailable)
	{
		VkPhysicalDeviceFeatures2 vkPhysicalDeviceFeatures2Probe =
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
			.pNext = &vkPhysicalDeviceLineRasterizationFeaturesEXT,
		};
		vkGetPhysicalDeviceFeatures2(gpInstanceManager->mVkPhysicalDevice, &vkPhysicalDeviceFeatures2Probe);

		mbSmoothLinesEnabled = (vkPhysicalDeviceLineRasterizationFeaturesEXT.smoothLines == VK_TRUE)
		                    && (vkPhysicalDeviceLineRasterizationFeaturesEXT.rectangularLines == VK_TRUE);

		// Strip the smoothLines feature struct of probe-only flags; only the bits we want enabled remain
		vkPhysicalDeviceLineRasterizationFeaturesEXT = VkPhysicalDeviceLineRasterizationFeaturesEXT
		{
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT,
			.pNext = nullptr,
			.rectangularLines = mbSmoothLinesEnabled ? VK_TRUE : VK_FALSE,
			.smoothLines = mbSmoothLinesEnabled ? VK_TRUE : VK_FALSE,
		};

		if (mbSmoothLinesEnabled)
		{
			deviceExtensions.push_back(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME);
			LOG(kGraphics, kInfo, "VK_EXT_line_rasterization smooth lines enabled");
		}
	}

	mbWideLinesEnabled = gpInstanceManager->mVkPhysicalDeviceFeatures2.features.wideLines == VK_TRUE;

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

	// Build feature pNext chain tail: maintenance9 (if available) -> shader clock (if enabled) -> line rasterization (if smooth lines enabled)
	void* pFeatureChainTail = nullptr;
	if constexpr (kbShaderRealtimeClock)
	{
		pFeatureChainTail = &vkPhysicalDeviceShaderClockFeaturesKHR;
	}
	if (bMaintenance9Available)
	{
		vkPhysicalDeviceMaintenance9FeaturesKHR.pNext = pFeatureChainTail;
		pFeatureChainTail = &vkPhysicalDeviceMaintenance9FeaturesKHR;
	}
	if (mbSmoothLinesEnabled)
	{
		vkPhysicalDeviceLineRasterizationFeaturesEXT.pNext = pFeatureChainTail;
		pFeatureChainTail = &vkPhysicalDeviceLineRasterizationFeaturesEXT;
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
		.descriptorBindingPartiallyBound = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
	};
	if constexpr (kbGpuAssistedValidation || kbDebugPrintf)
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
	vkDeviceCreateInfo.enabledLayerCount = kbVulkanDebugLayers ? static_cast<uint32_t>(gpInstanceManager->mValidationLayers.size()) : 0;
	vkDeviceCreateInfo.ppEnabledLayerNames = kbVulkanDebugLayers ? gpInstanceManager->mValidationLayers.data() : nullptr;
	vkDeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	vkDeviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	VkPhysicalDeviceFeatures vkPhysicalDeviceFeatures
	{
		.sampleRateShading = VK_TRUE,
		.samplerAnisotropy = VK_TRUE,
		.textureCompressionBC = VK_TRUE,
		.shaderInt64 = kbShaderRealtimeClock ? VK_TRUE : VK_FALSE,
	#if !defined(ENABLE_32_BIT_BOOL)
		.shaderInt16 = VK_TRUE,
	#endif
	};
	vkPhysicalDeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
	if (mbWideLinesEnabled)
	{
		vkPhysicalDeviceFeatures.wideLines = VK_TRUE;
	}
	if constexpr (kbVulkanWireframe)
	{
		vkPhysicalDeviceFeatures.fillModeNonSolid = VK_TRUE;
	}
	if constexpr (kbGpuAssistedValidation || kbDebugPrintf)
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

	// Shared command pool for OneShotCommandBuffer (single-threaded, graphics queue only)
	VkCommandPoolCreateInfo oneShotCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(mVkDevice, &oneShotCommandPoolCreateInfo, nullptr, &mOneShotVkCommandPool));
	VkName(VK_OBJECT_TYPE_COMMAND_POOL, mOneShotVkCommandPool, "OneShot");

	VkFenceCreateInfo oneShotFenceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};
	CHECK_VK(vkCreateFence(mVkDevice, &oneShotFenceCreateInfo, nullptr, &mOneShotVkFence));
	VkName(VK_OBJECT_TYPE_FENCE, mOneShotVkFence, "OneShot");

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
		LOG(kGraphics, kInfo, "Transfer queue: shared with graphics queue (family {}), background GPU uploads disabled", gpInstanceManager->miGraphicsQueueFamilyIndex);
	}
	else
	{
		LOG(kGraphics, kInfo, "Transfer queue: dedicated (family {}), background GPU uploads enabled", gpInstanceManager->miTransferQueueFamilyIndex);
	}

	// Query whether QFOT is optional for transfer -> graphics
	if (bMaintenance9Available && gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex)
	{
		uint32_t uiQueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties2(gpInstanceManager->mVkPhysicalDevice, &uiQueueFamilyCount, nullptr);
		std::vector<VkQueueFamilyOwnershipTransferPropertiesKHR> queueFamilyOwnershipTransferProperties(uiQueueFamilyCount, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_OWNERSHIP_TRANSFER_PROPERTIES_KHR, .pNext = nullptr});
		std::vector<VkQueueFamilyProperties2> queueFamilyProperties2(uiQueueFamilyCount, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2, .pNext = nullptr});
		for (uint32_t i = 0; i < uiQueueFamilyCount; ++i)
		{
			queueFamilyProperties2[i].pNext = &queueFamilyOwnershipTransferProperties[i];
		}
		vkGetPhysicalDeviceQueueFamilyProperties2(gpInstanceManager->mVkPhysicalDevice, &uiQueueFamilyCount, queueFamilyProperties2.data());

		uint32_t uiTransferFamily = static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex);
		uint32_t uiGraphicsFamily = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex);
		uint32_t uiOptimalMask = queueFamilyOwnershipTransferProperties[uiTransferFamily].optimalImageTransferToQueueFamilies;
		mbTransferQueueFamilyOwnershipTransferOptional = (uiOptimalMask & (1u << uiGraphicsFamily)) != 0;
		LOG(kGraphics, kDebug, "Transfer->Graphics QFOT optional: {} (transfer family {} optimal mask {:#010b}, graphics family {})", mbTransferQueueFamilyOwnershipTransferOptional, uiTransferFamily, uiOptimalMask, uiGraphicsFamily);
	}

	// Descriptor pool
	VkDescriptorPoolSize pVkDescriptorPoolSizes[]
	{
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 256},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4096},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 8},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 2 * 1024},
		VkDescriptorPoolSize {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1024},
	};
	VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
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

	// Pipeline cache
	if constexpr (kbVulkanPipelineCache)
	{
		VkPipelineCacheCreateInfo vkPipelineCacheCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.initialDataSize = 0,
			.pInitialData = nullptr,
		};
		std::vector<uint8_t> cacheData;
		if (gpFileManager->Exists({FileFlags::kAppDataDirectory}, "pipeline.cache"))
		{
			std::fstream fileStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, "pipeline.cache");
			fileStream.seekg(0, std::ios::end);
			int64_t iSize = fileStream.tellg();
			fileStream.seekg(0, std::ios::beg);
			cacheData.resize(iSize);
			fileStream.read(reinterpret_cast<char*>(cacheData.data()), iSize);

			// Pre-validate header — NVIDIA logs a warning instead of silently discarding incompatible entries
			bool bCompatible = false;
			if (cacheData.size() >= sizeof(VkPipelineCacheHeaderVersionOne))
			{
				VkPipelineCacheHeaderVersionOne header;
				std::memcpy(&header, cacheData.data(), sizeof(header));
				const VkPhysicalDeviceProperties& rProps = gpInstanceManager->mVkPhysicalDeviceProperties;
				bCompatible = header.headerSize == sizeof(VkPipelineCacheHeaderVersionOne)
					&& header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
					&& header.vendorID == rProps.vendorID
					&& header.deviceID == rProps.deviceID
					&& std::memcmp(header.pipelineCacheUUID, rProps.pipelineCacheUUID, VK_UUID_SIZE) == 0;
			}

			if (bCompatible)
			{
				vkPipelineCacheCreateInfo.initialDataSize = cacheData.size();
				vkPipelineCacheCreateInfo.pInitialData = cacheData.data();
				LOG(kGraphics, kDebug, "Loaded pipeline cache ({} bytes)", iSize);
			}
			else
			{
				LOG(kGraphics, kDebug, "Discarded incompatible pipeline cache ({} bytes)", iSize);
			}
		}
		CHECK_VK(vkCreatePipelineCache(mVkDevice, &vkPipelineCacheCreateInfo, nullptr, &mVkPipelineCache));
	}
}

DeviceManager::~DeviceManager()
{
	// Save pipeline cache to disk
	if constexpr (kbVulkanPipelineCache)
	{
		size_t uiDataSize = 0;
		vkGetPipelineCacheData(mVkDevice, mVkPipelineCache, &uiDataSize, nullptr);
		std::vector<uint8_t> cacheData(uiDataSize);
		vkGetPipelineCacheData(mVkDevice, mVkPipelineCache, &uiDataSize, cacheData.data());
		static_cast<void>(gpFileManager->WriteFileAtomically({FileFlags::kAppDataDirectory, FileFlags::kWrite}, "pipeline.cache", [&](std::fstream& rStream)
		{
			rStream.write(reinterpret_cast<const char*>(cacheData.data()), static_cast<std::streamsize>(uiDataSize));
		}));
		LOG(kGraphics, kDebug, "Saved pipeline cache ({} bytes)", uiDataSize);
		vkDestroyPipelineCache(mVkDevice, mVkPipelineCache, nullptr);
	}

	vmaDestroyAllocator(mpAllocator);
	mpAllocator = nullptr;

	// All descriptor sets freed explicitly in Pipeline::Destroy() before reaching here
	vkDestroyDescriptorPool(gpDeviceManager->mVkDevice, mVkDescriptorPool, nullptr);

	vkDestroyFence(mVkDevice, mOneShotVkFence, nullptr);
	vkDestroyCommandPool(mVkDevice, mOneShotVkCommandPool, nullptr);

	vkDestroyDevice(mVkDevice, nullptr);

	gpDeviceManager = nullptr;
}

} // namespace engine
