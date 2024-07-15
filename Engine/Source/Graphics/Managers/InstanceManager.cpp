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
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME,
#endif
};

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData)
{
	if (pCallbackData->messageIdNumber == 0x745174a4    || // VUID-NONE
#if defined(ENABLE_RENDER_THREAD)
		// DT: TODO Render thread confusing validations?
		pCallbackData->messageIdNumber == 0x42f2f4ed    || // SYNC-HAZARD-WRITE-AFTER-PRESENT
		pCallbackData->messageIdNumber == 0x376bc9df    || // SYNC-HAZARD-WRITE-AFTER-READ
		pCallbackData->messageIdNumber == 0xa05b236e    || // UNASSIGNED-Threading-MultipleThreads-Write
#endif
		pCallbackData->messageIdNumber == 0xb8515d13    || // WARNING-cache-file-error
		pCallbackData->messageIdNumber == 0xf0bb3995    || // UNASSIGNED-cache-file-error
		pCallbackData->messageIdNumber == 0x58781063    || // UNASSIGNED-BestPractices-vkAllocateMemory-too-many-objects
		pCallbackData->messageIdNumber == 0x654358b5    || // UNASSIGNED-BestPractices-vkCreateSwapchainKHR-suboptimal-swapchain-image-count
		pCallbackData->messageIdNumber == 0x8928392f    || // UNASSIGNED-BestPractices-Failure-Result
		pCallbackData->messageIdNumber == 0x9f63e654    || // UNASSIGNED-BestPractices-TransitionUndefinedToReadOnly
		pCallbackData->messageIdNumber == 0xe9c48aff    || // UNASSIGNED-BestPractices-NonSuccess-Result
		pCallbackData->messageIdNumber == 0x1cc8223af28 || // UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation
		pCallbackData->messageIdNumber == 0x27339f08778 || // UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation
		pCallbackData->messageIdNumber == 0xb3d4346b    || // UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation
		pCallbackData->messageIdNumber == 0x54ede350    || // BestPractices-vkCreateSwapchainKHR-suboptimal-swapchain-image-count
		pCallbackData->messageIdNumber == 0xdc18ad6b)      // UNASSIGNED-BestPractices-vkAllocateMemory-small-allocation
	{
		return VK_FALSE;
	}

	if (strstr(pCallbackData->pMessageIdName, "BestPractices") != 0)
	{
		return VK_FALSE;
	}

	// Validation layers get this wrong
	if (strstr(pCallbackData->pMessage, "ShaderClockKHR") != nullptr || strstr(pCallbackData->pMessage, "SPV_KHR_shader_clock") != nullptr)
	{
		return VK_FALSE;
	}

	if (pCallbackData->messageIdNumber == 0x92394c89)
	{
		auto message = std::string(pCallbackData->pMessage);
		auto it = message.rfind('|'); ++it; ++it;
		message = message.substr(it);
		LOG("[debugPrintfEXT] {}", message);

		if (message.find("DebugLog") != std::string::npos)
		{
			static constexpr int64_t kiElements = 8;
			static float spfElements[kiElements] = {};

			std::vector<std::string> splits = common::Split(message, std::string(","));
			float pfElements[kiElements] = {};
			for (int64_t i = 1; i < kiElements; ++i)
			{
				pfElements[i] = std::stof(splits[i].data());
			}

			static bool sbFirst = true;
			if (sbFirst)
			{
				LOG("Diff {} {} {} {} {} {} {}", pfElements[1] - spfElements[1], pfElements[2] - spfElements[2], pfElements[3] - spfElements[3], pfElements[4] - spfElements[4], pfElements[5] - spfElements[5], pfElements[6] - spfElements[6], pfElements[7] - spfElements[7]);

				float fTimeDiff = pfElements[3] - spfElements[3];
				if (fTimeDiff < 0.006f || fTimeDiff > 0.014f)
				{
					LOG("\nfTimeDiff: {}\n", fTimeDiff);
				}
			}

			for (int64_t i = 1; i < kiElements; ++i)
			{
				if (sbFirst)
				{
					spfElements[i] = pfElements[i];
				}
				else
				{
					ASSERT(pfElements[i] == spfElements[i]);
				}
			}

			sbFirst = !sbFirst;
		}

		return VK_FALSE;
	}

	LOG("DebugUtilsCallback {} {} \"{}\"", static_cast<uint64_t>(messageSeverity), static_cast<uint64_t>(messageType), pCallbackData->pMessage);

	if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0 || (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
	{
		DEBUG_BREAK();
	}

	if ((messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0 || (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0)
	{
		DEBUG_BREAK();
	}

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
		.pApplicationName = game::kpcGameName.data(),
		.applicationVersion = game::kiGameVersion,
		.pEngineName = nullptr,
		.engineVersion = 0,
	#if defined(ENABLE_VULKAN_1_2)
		.apiVersion = VK_API_VERSION_1_2, // Also update "--target-env vulkan1.2" in DataPacker
	#else
		.apiVersion = VK_API_VERSION_1_1, // Also update "--target-env vulkan1.1" in DataPacker
	#endif
	};
	VkValidationFeatureEnableEXT pVkValidationFeatureEnableEXT[] =
	{
	#if defined(ENABLE_DEBUG_PRINTF_EXT)
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	#else
		// Temporarily disabled: Causes 2fps in SDK 1.3.275.0
		// VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		// VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	#endif
		VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	};
	VkValidationFeaturesEXT vkValidationFeaturesEXT =
	{
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = static_cast<uint32_t>(std::size(pVkValidationFeatureEnableEXT)),
		.pEnabledValidationFeatures = pVkValidationFeatureEnableEXT,
		.disabledValidationFeatureCount = 0,
		.pDisabledValidationFeatures = nullptr,
	};
#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	static_assert(std::size(kppcInstanceExtensionNames) == 7);
#else
	static_assert(std::size(kppcInstanceExtensionNames) == 4);
#endif
	VkInstanceCreateInfo vkInstanceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
	#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
		.pNext = &vkValidationFeaturesEXT,
	#else
		.pNext = nullptr,
	#endif
		.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
		.pApplicationInfo = &vkApplicationInfo,
	#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
		.enabledLayerCount = static_cast<uint32_t>(mValidationLayers.size()),
		.ppEnabledLayerNames = mValidationLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(mbFoundKhronosValidation ? std::size(kppcInstanceExtensionNames) : std::size(kppcInstanceExtensionNames) - 3),
	#else
		.enabledExtensionCount = static_cast<uint32_t>(std::size(kppcInstanceExtensionNames)),
	#endif
		.ppEnabledExtensionNames = kppcInstanceExtensionNames,
	};

	HMODULE renderDocHmodule = GetModuleHandle("renderdoc.dll");
	LOG("renderDocHmodule: {}", reinterpret_cast<uint64_t>(renderDocHmodule));
	if (renderDocHmodule != 0)
	{
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
		LOG("vkCreateInstance returned {}, re-trying with VK_API_VERSION_1_0", vkResultCreateInstance);

		vkApplicationInfo.apiVersion = VK_API_VERSION_1_0;
		CHECK_VK(vkCreateInstance(&vkInstanceCreateInfo, nullptr, &mVkInstance));
	}

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	mVkSetDebugUtilsObjectNameEXT = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(mVkInstance, "vkSetDebugUtilsObjectNameEXT"));

	// Set up a callback to receive messages from the debug utils validation layer
	VkDebugUtilsMessengerCreateInfoEXT vkDebugUtilsMessengerCreateInfoEXT
	{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = nullptr,
		.flags = 0,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = DebugUtilsCallback,
	};

	auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(mVkInstance, "vkCreateDebugUtilsMessengerEXT"));
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
		throw std::exception("No physical devices found");
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
	LOG("  maxImageDimension2D: {}", mVkPhysicalDeviceProperties.limits.maxImageDimension2D);
	LOG("  maxImageDimensionCube: {}", mVkPhysicalDeviceProperties.limits.maxImageDimensionCube);
	LOG("  maxPerStageResources: {}", mVkPhysicalDeviceProperties.limits.maxPerStageResources);
	ASSERT(mVkPhysicalDeviceProperties.limits.maxUniformBufferRange >= 65536);
	vkGetPhysicalDeviceMemoryProperties(mVkPhysicalDevice, &mVkPhysicalDeviceMemoryProperties);

	vkGetPhysicalDeviceFeatures2(mVkPhysicalDevice, &mVkPhysicalDeviceFeatures2);
	ASSERT(mVkPhysicalDeviceFeatures2.features.sampleRateShading == VK_TRUE);
	ASSERT(mVkPhysicalDeviceFeatures2.features.samplerAnisotropy == VK_TRUE);
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
	ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderSubgroupClock == VK_TRUE);
	ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderDeviceClock == VK_TRUE);
#endif
#if defined(ENABLE_VULKAN_8BIT)
	ASSERT(mVkPhysicalDeviceVulkan12Features.storageBuffer8BitAccess == VK_TRUE);
	ASSERT(mVkPhysicalDeviceVulkan12Features.shaderInt8 == VK_TRUE);
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
	std::vector<VkQueueFamilyProperties> physicalDeviceQueueFamilies(uiPhysicalDeviceQueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(mVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, physicalDeviceQueueFamilies.data());
	ASSERT(uiPhysicalDeviceQueueFamilyCount != 0);

	LOG("Physical device queues ({}):", uiPhysicalDeviceQueueFamilyCount);
	for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
	{
		VkBool32 supportsPresentVkBool32 = VK_FALSE;
		CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(mVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
		LOG("  {} | {} | {} | {}", (physicalDeviceQueueFamilies.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 ? "VK_QUEUE_GRAPHICS_BIT" : "                     ", supportsPresentVkBool32 == VK_TRUE ? "Supports present" : "                ", (physicalDeviceQueueFamilies.at(i).queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 ? "VK_QUEUE_COMPUTE_BIT" : "                    ", (physicalDeviceQueueFamilies.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 ? "VK_QUEUE_TRANSFER_BIT" : "                     ");
	}
	LOG("");

	miGraphicsQueueFamilyIndex = UINT32_MAX;
	miPresentQueueFamilyIndex = UINT32_MAX;
	miTransferQueueFamilyIndex = UINT32_MAX;
	for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
	{
		if ((physicalDeviceQueueFamilies.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
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

		if ((physicalDeviceQueueFamilies.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) != 0)
		{
			// Look for a queue that supports only transfer, this will be the fastest for concurrent uploads (won't stall the other queues)
			if ((physicalDeviceQueueFamilies.at(i).queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == 0)
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
		LOG("  {} ({})", gEnumToString.mVkFormatToStringMap.at(rVkSurfaceFormatKHR.format), gEnumToString.mVkColorSpaceKHRToStringMap.find(rVkSurfaceFormatKHR.colorSpace)->second);
	}
	LOG("");

	// If the format list includes just one entry of VK_FORMAT_UNDEFINED, the surface has no preferred format
	if (uiFormatCount == 1 && physicalDeviceSurfaceFormats.at(0).format == VK_FORMAT_UNDEFINED)
	{
		mFramebufferVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else
	{
		for (const VkSurfaceFormatKHR& rVkSurfaceFormatKHR : physicalDeviceSurfaceFormats)
		{
			if (rVkSurfaceFormatKHR.format == VK_FORMAT_B8G8R8A8_UNORM || rVkSurfaceFormatKHR.format == VK_FORMAT_R8G8B8A8_UNORM)
			{
				mFramebufferVkFormat = rVkSurfaceFormatKHR.format;
				break;
			}
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
			LOG("Depth format selected: {} (optimal)\n", gEnumToString.mVkFormatToStringMap.at(rFormat));
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
				LOG("Depth format selected: {} (linear)\n", gEnumToString.mVkFormatToStringMap.at(rFormat));
				mDepthVkFormat = rFormat;
				break;
			}
		}
	}

	if (mDepthVkFormat == VK_FORMAT_UNDEFINED)
	{
		throw std::exception("Unable to find VkFormat for depth buffer");
	}
}

InstanceManager::~InstanceManager()
{
	vkDestroySurfaceKHR(mVkInstance, mVkSurfaceKHR, nullptr);

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(mVkInstance, "vkDestroyDebugUtilsMessengerEXT"));
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
