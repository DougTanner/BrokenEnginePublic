#include "InstanceManager.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

#include "Game.h"

namespace engine
{

constexpr char kpcKhronosValidation[] = "VK_LAYER_KHRONOS_validation";
constexpr const char* kppcValidationLayers[]
{
	kpcKhronosValidation,
	"VK_LAYER_KHRONOS_synchronization2",
};

const char* kppcInstanceExtensionNames[]
{
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
#endif
};

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData)
{
	if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "TransitionUndefinedToReadOnly") != nullptr)
	{
		// Lazy texture loading: We intentionally transition empty textures from UNDEFINED to SHADER_READ_ONLY. Reading undefined contents is fine, textures will be updated later.
		return VK_FALSE;
	}

	if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "VkDescriptorSetAllocateInfo-descriptorCount") != nullptr)
	{
		LOG("Double the number of descriptor sets in DeviceManager::DeviceManager() {}", pCallbackData->pMessage);
		ASSERT(false);
		return VK_FALSE;
	}

	if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "DEBUG-PRINTF") != nullptr)
	{
		auto message = std::string(pCallbackData->pMessage);
		std::vector<std::string> splits = common::Split(message, std::string("\n"));
		LOG("[debugPrintfEXT] {}", splits.back());
		return VK_FALSE;
	}

	if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0 || (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
	{
		return VK_FALSE;
	}

	LOG("DebugUtilsCallback {} {} \"{}\" \"{}\"", static_cast<uint64_t>(messageSeverity), static_cast<uint64_t>(messageType), pCallbackData->pMessageIdName, pCallbackData->pMessage);
	DEBUG_BREAK();

	return VK_FALSE;
}
#endif

VkSampleCountFlagBits SelectSampleCount(VkSampleCountFlags eVkSampleCountFlags)
{
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_64_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_64_BIT;
	}
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_32_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_32_BIT;
	}
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_16_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_16_BIT;
	}
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_8_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_8_BIT;
	}
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_4_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_4_BIT;
	}
	if ((eVkSampleCountFlags & VK_SAMPLE_COUNT_2_BIT) != 0)
	{
		return VK_SAMPLE_COUNT_2_BIT;
	}

	return VK_SAMPLE_COUNT_1_BIT;
}

InstanceManager::InstanceManager(HINSTANCE hinstance, HWND hwnd)
: mHinstance(hinstance)
, mHwnd(hwnd)
{
	gpInstanceManager = this;

	SCOPED_BOOT_TIMER(kBootTimerInstanceManager);

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	ReadLayerProperties();
#endif

	VkApplicationInfo vkApplicationInfo
	{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = game::kGameName.data(),
		.applicationVersion = game::kiGameVersion,
		.pEngineName = nullptr,
		.engineVersion = 0,
		.apiVersion = VK_API_VERSION_1_2, // Also update "--target-env vulkan1.2" in DataPacker
	};
	// Configure validation layer settings using VK_EXT_layer_settings
	[[maybe_unused]] VkBool32 vkTrue = VK_TRUE;
	[[maybe_unused]] VkBool32 vkFalse = VK_FALSE;
#if defined(ENABLE_GPU_ASSISTED_VALIDATION)
	const char* pcGpuBasedValue = "GPU_BASED_GPU_ASSISTED";
#elif defined(ENABLE_DEBUG_PRINTF_EXT)
	const char* pcGpuBasedValue = "GPU_BASED_DEBUG_PRINTF";
#endif

	VkLayerSettingEXT pVkLayerSettings[] =
	{
		{
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "validate_best_practices",
			.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
			.valueCount = 1,
			.pValues = &vkTrue,
		},
		{
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "validate_sync",
			.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
			.valueCount = 1,
			.pValues = &vkTrue,
		},
#if defined(ENABLE_GPU_ASSISTED_VALIDATION) || defined(ENABLE_DEBUG_PRINTF_EXT)
		{
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "validate_gpu_based",
			.type = VK_LAYER_SETTING_TYPE_STRING_EXT,
			.valueCount = 1,
			.pValues = &pcGpuBasedValue,
		},
#endif
#if defined(ENABLE_GPU_ASSISTED_VALIDATION)
		{
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "gpuav_validate_ray_query",
			.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
			.valueCount = 1,
			.pValues = &vkFalse,
		},
#endif
	};
#if defined(ENABLE_GPU_ASSISTED_VALIDATION)
	VkValidationFeatureEnableEXT pVkValidationFeatureEnables[] =
	{
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	};
	VkValidationFeaturesEXT vkValidationFeaturesEXT =
	{
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.pNext = nullptr,
		.enabledValidationFeatureCount = static_cast<uint32_t>(std::size(pVkValidationFeatureEnables)),
		.pEnabledValidationFeatures = pVkValidationFeatureEnables,
		.disabledValidationFeatureCount = 0,
		.pDisabledValidationFeatures = nullptr,
	};
#elif defined(ENABLE_DEBUG_PRINTF_EXT)
	VkValidationFeatureEnableEXT vkValidationFeatureEnableEXT =
	{
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	};
	VkValidationFeaturesEXT vkValidationFeaturesEXT =
	{
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.pNext = nullptr,
		.enabledValidationFeatureCount = 1,
		.pEnabledValidationFeatures = &vkValidationFeatureEnableEXT,
		.disabledValidationFeatureCount = 0,
		.pDisabledValidationFeatures = nullptr,
	};
#endif
	VkLayerSettingsCreateInfoEXT vkLayerSettingsCreateInfoEXT =
	{
		.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
	#if defined(ENABLE_GPU_ASSISTED_VALIDATION) || defined(ENABLE_DEBUG_PRINTF_EXT)
		.pNext = &vkValidationFeaturesEXT,
	#else
		.pNext = nullptr,
	#endif
		.settingCount = static_cast<uint32_t>(std::size(pVkLayerSettings)),
		.pSettings = pVkLayerSettings,
	};
	VkInstanceCreateInfo vkInstanceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
		.pNext = &vkLayerSettingsCreateInfoEXT,
	#else
		.pNext = nullptr,
	#endif
		.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
		.pApplicationInfo = &vkApplicationInfo,
	#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
		.enabledLayerCount = static_cast<uint32_t>(mValidationLayers.size()),
		.ppEnabledLayerNames = mValidationLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(mbFoundKhronosValidation ? std::size(kppcInstanceExtensionNames) : std::size(kppcInstanceExtensionNames) - (std::size(kppcInstanceExtensionNames) - std::size(kppcInstanceExtensionNames))),
	#else
		.enabledExtensionCount = static_cast<uint32_t>(std::size(kppcInstanceExtensionNames)),
	#endif
		.ppEnabledExtensionNames = kppcInstanceExtensionNames,
	};

	HMODULE renderDocHmodule = GetModuleHandle("renderdoc.dll");
	if (renderDocHmodule != 0)
	{
		LOG("renderDocHmodule: {}", reinterpret_cast<uint64_t>(renderDocHmodule));

		// Some extensions are not compatible with RenderDoc
		vkInstanceCreateInfo.enabledLayerCount = 0;
		vkInstanceCreateInfo.enabledExtensionCount = 2;
	}

	VkResult vkResultCreateInstance = vkCreateInstance(&vkInstanceCreateInfo, nullptr, &mVkInstance);

	if (vkResultCreateInstance != VK_SUCCESS)
	{
		// If the Vulkan SDK is not installed, validation layers will fail
		LOG("vkCreateInstance returned {}, re-trying with only 2 extensions", vkResultCreateInstance);

		vkInstanceCreateInfo.enabledLayerCount = 0;
		vkInstanceCreateInfo.enabledExtensionCount = 2;
		vkResultCreateInstance = vkCreateInstance(&vkInstanceCreateInfo, nullptr, &mVkInstance);
	}

	if (vkResultCreateInstance != VK_SUCCESS)
	{
		const char* pcResult = gEnumToString.Convert(vkResultCreateInstance);
		LOG("vkCreateInstance failed with {}, Vulkan 1.2 is required", pcResult);
		std::string errorMessage = "Failed to create Vulkan instance.\n\nVulkan 1.2 or higher is required.\n\nError: ";
		errorMessage += pcResult;

		MessageBox(nullptr, errorMessage.c_str(), game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

		throw std::runtime_error("Vulkan 1.2 not available");
	}

	// Load instance-specific Vulkan functions via Volk
	volkLoadInstance(mVkInstance);

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	// Set up a callback to receive messages from the debug utils validation layer
	VkDebugUtilsMessengerCreateInfoEXT vkDebugUtilsMessengerCreateInfoEXT
	{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = nullptr,
		.flags = 0,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = DebugUtilsCallback,
	};

	if (vkCreateDebugUtilsMessengerEXT != nullptr)
	{
		CHECK_VK(vkCreateDebugUtilsMessengerEXT(mVkInstance, &vkDebugUtilsMessengerCreateInfoEXT, nullptr, &mVkDebugUtilsMessengerEXT));
	}
#endif

	// Based on https://github.com/Overv/VulkanTutorial
	// Since Vulkan is a platform agnostic API, it can not interface directly with the window system on its own
	// To establish the connection between Vulkan and the window system to present results to the screen, we need to use platform-specific extensions
	// It exposes a VkSurfaceKHR object that represents an abstract type of surface to present rendered images to
	// The surface in our program will be backed by the window that we've already opened with GLFW or Android
	VkWin32SurfaceCreateInfoKHR vkWin32SurfaceCreateInfoKHR
	{
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.hinstance = mHinstance,
		.hwnd = mHwnd,
	};
	CHECK_VK(vkCreateWin32SurfaceKHR(mVkInstance, &vkWin32SurfaceCreateInfoKHR, nullptr, &mVkSurfaceKHR));

	// Look for and select a graphics card in the system that supports the features we need
	uint32_t uiPhysicalDeviceCount = 0;
	LOG("\nEnumerate physical devices");
	CHECK_VK(vkEnumeratePhysicalDevices(mVkInstance, &uiPhysicalDeviceCount, nullptr));
	LOG("  uiPhysicalDeviceCount: {}", uiPhysicalDeviceCount);
	if (uiPhysicalDeviceCount == 0)
	{
		throw std::runtime_error("No physical devices found");
	}
	std::vector<VkPhysicalDevice> physicalDevices(uiPhysicalDeviceCount);
	VkResult vkResult = vkEnumeratePhysicalDevices(mVkInstance, &uiPhysicalDeviceCount, physicalDevices.data());
	if (vkResult != VK_SUCCESS && vkResult != VK_INCOMPLETE)
	{
		CHECK_VK(vkResult);
	}

	LOG("  Physical devices:");
	for (const VkPhysicalDevice& rVkPhysicalDevice : physicalDevices)
	{
		VkPhysicalDeviceProperties vkPhysicalDeviceProperties {};
		vkGetPhysicalDeviceProperties(rVkPhysicalDevice, &vkPhysicalDeviceProperties);
		LOG("    \"{}\"{}", vkPhysicalDeviceProperties.deviceName, vkPhysicalDeviceProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? " (Discrete) " : "");

		// Validate device supports required Vulkan API version
		uint32_t deviceApiVersion = vkPhysicalDeviceProperties.apiVersion;
		if (deviceApiVersion < VK_API_VERSION_1_2)
		{
			LOG("      Skipping device: API version {}.{}.{} < required 1.2.0", VK_VERSION_MAJOR(deviceApiVersion), VK_VERSION_MINOR(deviceApiVersion), VK_VERSION_PATCH(deviceApiVersion));
			continue;
		}

		bool bSupportsPresent = false;
		uint32_t uiPhysicalDeviceQueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(rVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, nullptr);
		for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
		{
			VkBool32 supportsPresentVkBool32 = VK_FALSE;
			CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(rVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
			bSupportsPresent |= supportsPresentVkBool32 == VK_TRUE;
		}

		if (!bSupportsPresent)
		{
			continue;
		}

		if (mVkPhysicalDevice == VK_NULL_HANDLE || vkPhysicalDeviceProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			ASSERT(vkPhysicalDeviceProperties.limits.maxPerStageResources - 16 >= data::kiTextureCount);
			ASSERT(vkPhysicalDeviceProperties.limits.maxPerStageResources - 16 >= data::kiUiTextureCount);
			static_assert(data::kiTextureCount < shaders::kiMaxTextureCount);
			static_assert(data::kiUiTextureCount < shaders::kiMaxTextureCount);
			mVkPhysicalDevice = rVkPhysicalDevice;
		}

		if (vkPhysicalDeviceProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			break;
		}
	}
	ASSERT(mVkPhysicalDevice != VK_NULL_HANDLE);

	vkGetPhysicalDeviceProperties(mVkPhysicalDevice, &mVkPhysicalDeviceProperties);
	LOG("  Selected device API version: {}.{}.{}", VK_VERSION_MAJOR(mVkPhysicalDeviceProperties.apiVersion), VK_VERSION_MINOR(mVkPhysicalDeviceProperties.apiVersion), VK_VERSION_PATCH(mVkPhysicalDeviceProperties.apiVersion));
	LOG("  maxImageDimension2D: {}", mVkPhysicalDeviceProperties.limits.maxImageDimension2D);
	LOG("  maxImageDimensionCube: {}", mVkPhysicalDeviceProperties.limits.maxImageDimensionCube);
	LOG("  maxPerStageResources: {}", mVkPhysicalDeviceProperties.limits.maxPerStageResources);
	ASSERT(mVkPhysicalDeviceProperties.limits.maxUniformBufferRange >= 65536);
	vkGetPhysicalDeviceMemoryProperties(mVkPhysicalDevice, &mVkPhysicalDeviceMemoryProperties);

	vkGetPhysicalDeviceFeatures2(mVkPhysicalDevice, &mVkPhysicalDeviceFeatures2);

	// Check required Vulkan 1.2 features
	if (mVkPhysicalDeviceVulkan12Features.descriptorBindingStorageBufferUpdateAfterBind != VK_TRUE)
	{
		MessageBox(nullptr, "Required Vulkan feature not supported.\n\ndescriptorBindingStorageBufferUpdateAfterBind is required for VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("descriptorBindingStorageBufferUpdateAfterBind not supported");
	}
	if (mVkPhysicalDeviceVulkan12Features.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
	{
		MessageBox(nullptr, "Required Vulkan feature not supported.\n\nshaderSampledImageArrayNonUniformIndexing is required for non-uniform descriptor indexing.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("shaderSampledImageArrayNonUniformIndexing not supported");
	}

	ASSERT(mVkPhysicalDeviceFeatures2.features.sampleRateShading == VK_TRUE);
	ASSERT(mVkPhysicalDeviceFeatures2.features.samplerAnisotropy == VK_TRUE);
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
	ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderSubgroupClock == VK_TRUE);
	ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderDeviceClock == VK_TRUE);
#endif
	ASSERT(mVkPhysicalDeviceFeatures2.features.textureCompressionBC == VK_TRUE);
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
	ASSERT(mVkPhysicalDeviceFeatures2.features.shaderInt64 == VK_TRUE);
#endif
#if !defined(ENABLE_32_BIT_BOOL)
	ASSERT(mVkPhysicalDevice16BitStorageFeatures.storageBuffer16BitAccess == VK_TRUE);
	ASSERT(mVkPhysicalDevice16BitStorageFeatures.uniformAndStorageBuffer16BitAccess == VK_TRUE);
	ASSERT(mVkPhysicalDeviceFeatures2.features.shaderInt16 == VK_TRUE);
#endif
	meMaxMultisampleCount = SelectSampleCount(mVkPhysicalDeviceProperties.limits.framebufferColorSampleCounts & mVkPhysicalDeviceProperties.limits.framebufferDepthSampleCounts);
	LOG("  Max multisample count: {}\n", static_cast<int64_t>(meMaxMultisampleCount));
	if (gSampleCount.Get<VkSampleCountFlagBits>() > meMaxMultisampleCount)
	{
		gSampleCount.Reset<VkSampleCountFlagBits>(meMaxMultisampleCount);
	}

	// There are different types of queues that originate from different queue families and each family of queues allows only a subset of commands
	// For example, there could be a queue family that only allows processing of compute commands or one that only allows memory transfer related commands
	uint32_t uiPhysicalDeviceQueueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(mVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, nullptr);
	ASSERT(uiPhysicalDeviceQueueFamilyCount != 0);
	mVkQueueFamilyProperties.resize(uiPhysicalDeviceQueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(mVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, mVkQueueFamilyProperties.data());
	ASSERT(uiPhysicalDeviceQueueFamilyCount != 0);

	LOG("Physical device queues ({}):", uiPhysicalDeviceQueueFamilyCount);
	for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
	{
		VkBool32 supportsPresentVkBool32 = VK_FALSE;
		CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(mVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
		LOG("  {} | {} | {} | {}", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 ? "VK_QUEUE_GRAPHICS_BIT" : "                     ", supportsPresentVkBool32 == VK_TRUE ? "Supports present" : "                ", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 ? "VK_QUEUE_COMPUTE_BIT" : "                    ", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 ? "VK_QUEUE_TRANSFER_BIT" : "                     ");
	}
	LOG("");

	miGraphicsQueueFamilyIndex = UINT32_MAX;
	miPresentQueueFamilyIndex = UINT32_MAX;
	miTransferQueueFamilyIndex = UINT32_MAX;
	for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
	{
		if ((mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
		{
			// Search for a graphics queue in the array of queue families, prefer one that supports both
			VkBool32 supportsPresentVkBool32 = VK_FALSE;
			CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(mVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
			if (supportsPresentVkBool32 == VK_TRUE)
			{
				miGraphicsQueueFamilyIndex = i;
				miPresentQueueFamilyIndex = i;
			}

			if (miGraphicsQueueFamilyIndex == UINT32_MAX)
			{
				miGraphicsQueueFamilyIndex = i;
			}
		}

		if ((mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) != 0)
		{
			// Look for a queue that supports only transfer, this will be the fastest for concurrent uploads (won't stall the other queues)
			if ((mVkQueueFamilyProperties.at(i).queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
			{
				miTransferQueueFamilyIndex = i;
			}

			if (miTransferQueueFamilyIndex == UINT32_MAX)
			{
				miTransferQueueFamilyIndex = i;
			}
		}
	}

	// If didn't find a queue that supports both graphics and present, then find a separate present queue
	if (miPresentQueueFamilyIndex == UINT32_MAX)
	{
		for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
		{
			VkBool32 supportsPresentVkBool32 = VK_FALSE;
			CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(mVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
			if (supportsPresentVkBool32 == VK_TRUE)
			{
				miPresentQueueFamilyIndex = i;
				break;
			}
		}
	}

	ASSERT(miGraphicsQueueFamilyIndex != UINT32_MAX && miPresentQueueFamilyIndex != UINT32_MAX);

	// Get the list of surface formats that are supported
	uint32_t uiFormatCount = 0;
	CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurfaceKHR, &uiFormatCount, nullptr));
	ASSERT(uiFormatCount != 0);
	std::vector<VkSurfaceFormatKHR> physicalDeviceSurfaceFormats(uiFormatCount);
	CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurfaceKHR, &uiFormatCount, physicalDeviceSurfaceFormats.data()));

	LOG("Surface formats ({}):", physicalDeviceSurfaceFormats.size());
	for ([[maybe_unused]] const VkSurfaceFormatKHR& rVkSurfaceFormatKHR : physicalDeviceSurfaceFormats)
	{
		LOG("  {} ({})", gEnumToString.Convert(rVkSurfaceFormatKHR.format), gEnumToString.Convert(rVkSurfaceFormatKHR.colorSpace));
	}
	LOG("");

	// If the format list includes just one entry of VK_FORMAT_UNDEFINED, the surface has no preferred format
	if (uiFormatCount == 1 && physicalDeviceSurfaceFormats.at(0).format == VK_FORMAT_UNDEFINED)
	{
		mFramebufferVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
		LOG("Selected framebuffer format: {} with color space: {} (no preferred format)\n", gEnumToString.Convert(mFramebufferVkFormat), gEnumToString.Convert(mFramebufferVkColorSpace));
	}
	else
	{
		bool bFoundPreferredFormat = false;
		for (const VkSurfaceFormatKHR& rVkSurfaceFormatKHR : physicalDeviceSurfaceFormats)
		{
			if (rVkSurfaceFormatKHR.format == VK_FORMAT_B8G8R8A8_UNORM || rVkSurfaceFormatKHR.format == VK_FORMAT_R8G8B8A8_UNORM)
			{
				mFramebufferVkFormat = rVkSurfaceFormatKHR.format;
				mFramebufferVkColorSpace = rVkSurfaceFormatKHR.colorSpace;
				bFoundPreferredFormat = true;
				LOG("Selected framebuffer format: {} with color space: {}\n", gEnumToString.Convert(mFramebufferVkFormat), gEnumToString.Convert(mFramebufferVkColorSpace));
				break;
			}
		}

		// Fallback to first available format if preferred formats not found
		if (!bFoundPreferredFormat)
		{
			mFramebufferVkFormat = physicalDeviceSurfaceFormats.at(0).format;
			mFramebufferVkColorSpace = physicalDeviceSurfaceFormats.at(0).colorSpace;
			LOG("Using fallback surface format: {} with color space: {} (preferred formats not available)\n", gEnumToString.Convert(mFramebufferVkFormat), gEnumToString.Convert(mFramebufferVkColorSpace));
		}
	}

	// Prefer high precision depth formats
	VkFormat pVkFormats[] {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT};

	// Search first for optimal formats
	for (VkFormat& rFormat : pVkFormats)
	{
		VkFormatProperties vkFormatProperties {};
		vkGetPhysicalDeviceFormatProperties(mVkPhysicalDevice, rFormat, &vkFormatProperties);

		if ((vkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
		{
			LOG("Depth format selected: {} (optimal)\n", gEnumToString.Convert(rFormat));
			mDepthVkFormat = rFormat;
			break;
		}
	}

	if (mDepthVkFormat == VK_FORMAT_UNDEFINED)
	{
		// Search linear if we can't find an optimal format
		for (VkFormat& rFormat : pVkFormats)
		{
			VkFormatProperties vkFormatProperties {};
			vkGetPhysicalDeviceFormatProperties(mVkPhysicalDevice, rFormat, &vkFormatProperties);

			if ((vkFormatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				LOG("Depth format selected: {} (linear)\n", gEnumToString.Convert(rFormat));
				mDepthVkFormat = rFormat;
				break;
			}
		}
	}

	if (mDepthVkFormat == VK_FORMAT_UNDEFINED)
	{
		throw std::runtime_error("Unable to find VkFormat for depth buffer");
	}
}

InstanceManager::~InstanceManager()
{
	vkDestroySurfaceKHR(mVkInstance, mVkSurfaceKHR, nullptr);

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	if (vkDestroyDebugUtilsMessengerEXT != nullptr)
	{
		vkDestroyDebugUtilsMessengerEXT(mVkInstance, mVkDebugUtilsMessengerEXT, nullptr);
	}
#endif

	vkDestroyInstance(mVkInstance, nullptr);

	gpInstanceManager = nullptr;
}

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
void InstanceManager::ReadLayerProperties()
{
	uint32_t uiLayerCount = 0;
	while (true)
	{
		VkResult vkResultEnumerateInstanceLayerProperties = vkEnumerateInstanceLayerProperties(&uiLayerCount, nullptr);
		if (vkResultEnumerateInstanceLayerProperties == VK_INCOMPLETE)
		{
			continue;
		}

		CHECK_VK(vkResultEnumerateInstanceLayerProperties);
		break;
	}

	LOG("\nFound {} Vulkan validation layers:", uiLayerCount);

	if (uiLayerCount == 0)
	{
		return;
	}

	std::vector<VkLayerProperties> instanceLayerProperties(uiLayerCount);
	CHECK_VK(vkEnumerateInstanceLayerProperties(&uiLayerCount, instanceLayerProperties.data()));

	for (const VkLayerProperties& rVkLayerProperties : instanceLayerProperties)
	{
		LOG("  {} {}.{}", rVkLayerProperties.layerName, VK_VERSION_PATCH(rVkLayerProperties.specVersion), rVkLayerProperties.implementationVersion);

		if (strcmp(rVkLayerProperties.layerName, kpcKhronosValidation) == 0)
		{
			mbFoundKhronosValidation = true;
		}

		for (size_t i = 0; i < std::size(kppcValidationLayers); ++i)
		{
			if (strcmp(rVkLayerProperties.layerName, kppcValidationLayers[i]) == 0)
			{
				mValidationLayers.push_back(kppcValidationLayers[i]);
			}
		}

		uint32_t uiExtensionPropertiesCount = 0;
		while (true)
		{
			VkResult vkResultEnumerateInstanceExtensionProperties = vkEnumerateInstanceExtensionProperties(rVkLayerProperties.layerName, &uiExtensionPropertiesCount, nullptr);
			if (vkResultEnumerateInstanceExtensionProperties == VK_INCOMPLETE)
			{
				continue;
			}

			CHECK_VK(vkResultEnumerateInstanceExtensionProperties);
			break;
		}

		if (uiExtensionPropertiesCount == 0)
		{
			continue;
		}

		std::vector<VkExtensionProperties> extensionProperties(uiExtensionPropertiesCount);
		CHECK_VK(vkEnumerateInstanceExtensionProperties(rVkLayerProperties.layerName, &uiExtensionPropertiesCount, extensionProperties.data()));
		for (const VkExtensionProperties& rVkExtensionProperties : extensionProperties)
		{
			LOG("    Extension: {} {}", rVkExtensionProperties.extensionName, VK_VERSION_PATCH(rVkExtensionProperties.specVersion));
		}
	}

	LOG("");
	for (const auto& pcLayer : mValidationLayers)
	{
		LOG("Found \"{}\"", pcLayer);
	}
	LOG("");
}

#endif // ENABLE_VULKAN_DEBUG_LAYERS

} // namespace engine
