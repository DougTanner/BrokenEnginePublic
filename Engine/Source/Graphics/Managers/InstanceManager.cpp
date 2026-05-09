#include "InstanceManager.h"

#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"

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
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,
};
constexpr uint32_t kiBaseExtensionCount = 3;
constexpr uint32_t kiDebugExtensionCount = 5;

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback([[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType, [[maybe_unused]] const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData)
{
	if constexpr (kbVulkanDebugLayers)
	{
		if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "TransitionUndefinedToReadOnly") != nullptr)
		{
			// Lazy texture loading: We intentionally transition empty textures from UNDEFINED to SHADER_READ_ONLY. Reading undefined contents is fine, textures will be updated later.
			return VK_FALSE;
		}

		// Suppress false positive: with VK_KHR_maintenance9, QFOT is optional for sampled/transfer images so VK_QUEUE_FAMILY_IGNORED barriers are spec-correct,
		// but the validation layer's ConcurrentUsageOfExclusiveImage check is not maintenance9-aware and reports cross-queue usage at command buffer recording time.
		// Only fires during startup (GeneratePbrLutBrdf draws referencing transfer-queue-uploaded textures) and potentially after window resize re-recording.
		// During regular rendering, command buffers are pre-recorded before textures are adopted so the check never runs against transfer-queue-uploaded images.
		if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "ConcurrentUsageOfExclusiveImage") != nullptr)
		{
			return VK_FALSE;
		}

		if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "VkDescriptorSetAllocateInfo-descriptorCount") != nullptr)
		{
			LOG(kDefault, kError, "Double the number of descriptor sets in DeviceManager::DeviceManager() {}", pCallbackData->pMessage);
			ASSERT(false);
			return VK_FALSE;
		}

		if (pCallbackData->pMessageIdName != nullptr && strstr(pCallbackData->pMessageIdName, "DEBUG-PRINTF") != nullptr)
		{
			std::string message = std::string(pCallbackData->pMessage);
			std::vector<std::string> splits = common::Split(message, std::string("\n"));
			LOG(kGraphics, kInfo, "[debugPrintfEXT] {}", splits.back());
			return VK_FALSE;
		}

		if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) != 0 || (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0)
		{
			return VK_FALSE;
		}

		LOG(kDefault, kError, "DebugUtilsCallback {} {} \"{}\" \"{}\"", static_cast<uint64_t>(messageSeverity), static_cast<uint64_t>(messageType), pCallbackData->pMessageIdName, pCallbackData->pMessage);
		DEBUG_BREAK();
	}

	return VK_FALSE;
}

static VkSampleCountFlagBits SelectSampleCount(VkSampleCountFlags eVkSampleCountFlags)
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

	ScopedBootTimer scopedBootTimer(kBootTimerInstanceManager);

	if constexpr (kbVulkanDebugLayers)
	{
		ReadLayerProperties();
	}

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
	const char* pcGpuBasedValue = nullptr;
	if constexpr (kbGpuAssistedValidation)
	{
		pcGpuBasedValue = "GPU_BASED_GPU_ASSISTED";
	}
	else if constexpr (kbDebugPrintf)
	{
		pcGpuBasedValue = "GPU_BASED_DEBUG_PRINTF";
	}

	VkLayerSettingEXT layerSettings[4]
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
	};
	uint32_t uiLayerSettingCount = 2;
	if constexpr (kbGpuAssistedValidation)
	{
		layerSettings[uiLayerSettingCount++] = {
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "validate_gpu_based",
			.type = VK_LAYER_SETTING_TYPE_STRING_EXT,
			.valueCount = 1,
			.pValues = &pcGpuBasedValue,
		};
		layerSettings[uiLayerSettingCount++] = {
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "gpuav_validate_ray_query",
			.type = VK_LAYER_SETTING_TYPE_BOOL32_EXT,
			.valueCount = 1,
			.pValues = &vkFalse,
		};
	}
	else if constexpr (kbDebugPrintf)
	{
		layerSettings[uiLayerSettingCount++] = {
			.pLayerName = kpcKhronosValidation,
			.pSettingName = "validate_gpu_based",
			.type = VK_LAYER_SETTING_TYPE_STRING_EXT,
			.valueCount = 1,
			.pValues = &pcGpuBasedValue,
		};
	}

	[[maybe_unused]] VkValidationFeatureEnableEXT pVkValidationFeatureEnables[] =
	{
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	};
	[[maybe_unused]] VkValidationFeaturesEXT vkValidationFeaturesEXT =
	{
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.pNext = nullptr,
		.enabledValidationFeatureCount = static_cast<uint32_t>(std::size(pVkValidationFeatureEnables)),
		.pEnabledValidationFeatures = pVkValidationFeatureEnables,
		.disabledValidationFeatureCount = 0,
		.pDisabledValidationFeatures = nullptr,
	};
	[[maybe_unused]] VkValidationFeatureEnableEXT vkValidationFeatureEnableEXT =
	{
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	};
	if constexpr (kbDebugPrintf && !kbGpuAssistedValidation)
	{
		vkValidationFeaturesEXT.enabledValidationFeatureCount = 1;
		vkValidationFeaturesEXT.pEnabledValidationFeatures = &vkValidationFeatureEnableEXT;
	}
	VkLayerSettingsCreateInfoEXT vkLayerSettingsCreateInfoEXT =
	{
		.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
		.pNext = (kbGpuAssistedValidation || kbDebugPrintf) ? &vkValidationFeaturesEXT : nullptr,
		.settingCount = uiLayerSettingCount,
		.pSettings = layerSettings,
	};
	VkInstanceCreateInfo vkInstanceCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = kbVulkanDebugLayers ? &vkLayerSettingsCreateInfoEXT : nullptr,
		.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
		.pApplicationInfo = &vkApplicationInfo,
		.enabledLayerCount = kbVulkanDebugLayers ? static_cast<uint32_t>(mValidationLayers.size()) : 0,
		.ppEnabledLayerNames = kbVulkanDebugLayers ? mValidationLayers.data() : nullptr,
		.enabledExtensionCount = kbVulkanDebugLayers ? kiDebugExtensionCount : kiBaseExtensionCount,
		.ppEnabledExtensionNames = kppcInstanceExtensionNames,
	};

	// Opt-in force-load: lets RenderDoc's "Attach to running instance" find us without launching through RenderDoc. Triggers the layer-disable branch below, so it's guarded by kbRenderDocAttach to avoid sacrificing validation in normal debug runs.
	if constexpr (kbRenderDocAttach)
	{
		if (GetModuleHandle("renderdoc.dll") == nullptr)
		{
			// Default RenderDoc installer doesn't add itself to PATH, so plain LoadLibrary("renderdoc.dll") fails. Read the install dir from the Vulkan loader's implicit-layer JSON registration — renderdoc.dll lives in the same folder.
			HKEY hKey = nullptr;
			if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				char valueName[MAX_PATH];
				for (DWORD i = 0; ; ++i)
				{
					DWORD cchValueName = MAX_PATH;
					if (RegEnumValue(hKey, i, valueName, &cchValueName, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
					{
						break;
					}
					if (strstr(valueName, "renderdoc.json") != nullptr)
					{
						char* lastSep = strrchr(valueName, '\\');
						if (lastSep != nullptr)
						{
							strcpy_s(lastSep + 1, MAX_PATH - (lastSep + 1 - valueName), "renderdoc.dll");
							LoadLibrary(valueName);
						}
						break;
					}
				}
				RegCloseKey(hKey);
			}

			// PATH-relative fallback for users who manually added RenderDoc to PATH
			if (GetModuleHandle("renderdoc.dll") == nullptr)
			{
				LoadLibrary("renderdoc.dll");
			}

			if (GetModuleHandle("renderdoc.dll") == nullptr)
			{
				LOG(kGraphics, kWarning, "kbRenderDocAttach=true but renderdoc.dll could not be loaded. Confirm RenderDoc is installed and registered in HKLM\\SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers.");
			}
		}
	}

	HMODULE renderDocHmodule = GetModuleHandle("renderdoc.dll");
	if (renderDocHmodule != nullptr)
	{
		LOG(kGraphics, kInfo, "renderDocHmodule: {}", reinterpret_cast<uint64_t>(renderDocHmodule));

		// RenderDoc doesn't ship VK_LAYER_KHRONOS_validation, so VK_EXT_layer_settings is unavailable. Keep first 4 entries of kppcInstanceExtensionNames (surface, win32 surface, portability enumeration, debug utils). pNext must be cleared because VkLayerSettingsCreateInfoEXT requires the layer settings extension we just dropped. VUID-VkInstanceCreateInfo-flags-06559 holds because VK_KHR_portability_enumeration is retained.
		vkInstanceCreateInfo.pNext = nullptr;
		vkInstanceCreateInfo.enabledLayerCount = 0;
		vkInstanceCreateInfo.ppEnabledLayerNames = nullptr;
		vkInstanceCreateInfo.enabledExtensionCount = 4;

		if constexpr (kbRenderDocAttach)
		{
			auto pfnGetApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(renderDocHmodule, "RENDERDOC_GetAPI"));
			if (pfnGetApi != nullptr)
			{
				pfnGetApi(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&mpRenderDocApi));
				LOG(kGraphics, kInfo, "RenderDoc API initialized: {}", reinterpret_cast<uint64_t>(mpRenderDocApi));
			}
		}
	}

	VkResult vkResultCreateInstance = vkCreateInstance(&vkInstanceCreateInfo, nullptr, &mVkInstance);

	if (vkResultCreateInstance != VK_SUCCESS)
	{
		// Clean-machine fallback: no Vulkan SDK => no validation layer. Drop pNext (layer settings), clear portability flag (its extension is also dropped), keep only surface + win32 surface.
		LOG(kGraphics, kWarning, "vkCreateInstance returned {}, re-trying with minimal configuration", vkResultCreateInstance);

		vkInstanceCreateInfo.pNext = nullptr;
		vkInstanceCreateInfo.flags = 0;
		vkInstanceCreateInfo.enabledLayerCount = 0;
		vkInstanceCreateInfo.ppEnabledLayerNames = nullptr;
		vkInstanceCreateInfo.enabledExtensionCount = 2;
		vkResultCreateInstance = vkCreateInstance(&vkInstanceCreateInfo, nullptr, &mVkInstance);
	}

	if (vkResultCreateInstance != VK_SUCCESS)
	{
		common::ScopedWorkbufferAllocation<const char*> pcResult = gEnumToString.Convert(vkResultCreateInstance, common::gpThreadLocal->mWorkbuffer);
		LOG(kDefault, kError, "vkCreateInstance failed with {}, Vulkan 1.2 is required", pcResult);
		std::string errorMessage = "Failed to create Vulkan instance.\n\nVulkan 1.2 or higher is required.\n\nError: ";
		errorMessage += static_cast<const char*>(pcResult);

		MessageBox(nullptr, errorMessage.c_str(), game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

		throw std::runtime_error("Vulkan 1.2 not available");
	}

	// Load instance-specific Vulkan functions via Volk
	volkLoadInstance(mVkInstance);

	if constexpr (kbVulkanDebugLayers)
	{
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
	}

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

	SelectPhysicalDevice();
	SelectQueueFamilies();
	SelectSurfaceFormat();
	SelectDepthFormat();
}

void InstanceManager::SelectPhysicalDevice()
{
	uint32_t uiPhysicalDeviceCount = 0;
	LOG(kGraphics, kInfo, "\nEnumerate physical devices");
	CHECK_VK(vkEnumeratePhysicalDevices(mVkInstance, &uiPhysicalDeviceCount, nullptr));
	LOG(kGraphics, kInfo, "  uiPhysicalDeviceCount: {}", uiPhysicalDeviceCount);
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

	LOG(kGraphics, kInfo, "  Physical devices:");
	for (const VkPhysicalDevice& rVkPhysicalDevice : physicalDevices)
	{
		VkPhysicalDeviceProperties vkPhysicalDeviceProperties {};
		vkGetPhysicalDeviceProperties(rVkPhysicalDevice, &vkPhysicalDeviceProperties);
		LOG(kGraphics, kInfo, "    \"{}\"{}", vkPhysicalDeviceProperties.deviceName, vkPhysicalDeviceProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? " (Discrete) " : "");

		// Validate device supports required Vulkan API version
		uint32_t uiDeviceApiVersion = vkPhysicalDeviceProperties.apiVersion;
		if (uiDeviceApiVersion < VK_API_VERSION_1_2)
		{
			LOG(kGraphics, kInfo, "      Skipping device: API version {}.{}.{} < required 1.2.0", VK_VERSION_MAJOR(uiDeviceApiVersion), VK_VERSION_MINOR(uiDeviceApiVersion), VK_VERSION_PATCH(uiDeviceApiVersion));
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
			ASSERT(vkPhysicalDeviceProperties.limits.maxPerStageResources > 200);
			mVkPhysicalDevice = rVkPhysicalDevice;
		}

		if (vkPhysicalDeviceProperties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			break;
		}
	}
	ASSERT(mVkPhysicalDevice != VK_NULL_HANDLE);

	vkGetPhysicalDeviceProperties(mVkPhysicalDevice, &mVkPhysicalDeviceProperties);
	LOG(kGraphics, kInfo, "  Selected device API version: {}.{}.{}", VK_VERSION_MAJOR(mVkPhysicalDeviceProperties.apiVersion), VK_VERSION_MINOR(mVkPhysicalDeviceProperties.apiVersion), VK_VERSION_PATCH(mVkPhysicalDeviceProperties.apiVersion));
	LOG(kGraphics, kInfo, "  maxImageDimension2D: {}", mVkPhysicalDeviceProperties.limits.maxImageDimension2D);
	LOG(kGraphics, kInfo, "  maxImageDimensionCube: {}", mVkPhysicalDeviceProperties.limits.maxImageDimensionCube);
	LOG(kGraphics, kInfo, "  maxPerStageResources: {}", mVkPhysicalDeviceProperties.limits.maxPerStageResources);
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
	if (mVkPhysicalDeviceVulkan12Features.descriptorBindingSampledImageUpdateAfterBind != VK_TRUE)
	{
		MessageBox(nullptr, "Required Vulkan feature not supported.\n\ndescriptorBindingSampledImageUpdateAfterBind is required for texture streaming.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("descriptorBindingSampledImageUpdateAfterBind not supported");
	}
	if (mVkPhysicalDeviceVulkan12Features.descriptorBindingPartiallyBound != VK_TRUE)
	{
		MessageBox(nullptr, "Required Vulkan feature not supported.\n\ndescriptorBindingPartiallyBound is required for bindless texture arrays.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("descriptorBindingPartiallyBound not supported");
	}
	if (mVkPhysicalDeviceVulkan12Features.runtimeDescriptorArray != VK_TRUE)
	{
		MessageBox(nullptr, "Required Vulkan feature not supported.\n\nruntimeDescriptorArray is required for bindless texture arrays.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("runtimeDescriptorArray not supported");
	}

	ASSERT(mVkPhysicalDeviceFeatures2.features.sampleRateShading == VK_TRUE);
	ASSERT(mVkPhysicalDeviceFeatures2.features.samplerAnisotropy == VK_TRUE);
	if constexpr (kbShaderRealtimeClock)
	{
		ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderSubgroupClock == VK_TRUE);
		ASSERT(mVkPhysicalDeviceShaderClockFeaturesKHR.shaderDeviceClock == VK_TRUE);
	}
	ASSERT(mVkPhysicalDeviceFeatures2.features.textureCompressionBC == VK_TRUE);
	if constexpr (kbShaderRealtimeClock)
	{
		ASSERT(mVkPhysicalDeviceFeatures2.features.shaderInt64 == VK_TRUE);
	}
#if !defined(ENABLE_32_BIT_BOOL)
	ASSERT(mVkPhysicalDevice16BitStorageFeatures.storageBuffer16BitAccess == VK_TRUE);
	ASSERT(mVkPhysicalDevice16BitStorageFeatures.uniformAndStorageBuffer16BitAccess == VK_TRUE);
	ASSERT(mVkPhysicalDeviceFeatures2.features.shaderInt16 == VK_TRUE);
#endif
	meMaxMultisampleCount = SelectSampleCount(mVkPhysicalDeviceProperties.limits.framebufferColorSampleCounts & mVkPhysicalDeviceProperties.limits.framebufferDepthSampleCounts);
	LOG(kGraphics, kInfo, "  Max multisample count: {}\n", static_cast<int64_t>(meMaxMultisampleCount));
	if (gSampleCount.Get<VkSampleCountFlagBits>() > meMaxMultisampleCount)
	{
		gSampleCount.Reset<VkSampleCountFlagBits>(meMaxMultisampleCount);
	}
}

void InstanceManager::SelectQueueFamilies()
{
	uint32_t uiPhysicalDeviceQueueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(mVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, nullptr);
	ASSERT(uiPhysicalDeviceQueueFamilyCount != 0);
	mVkQueueFamilyProperties.resize(uiPhysicalDeviceQueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(mVkPhysicalDevice, &uiPhysicalDeviceQueueFamilyCount, mVkQueueFamilyProperties.data());
	ASSERT(uiPhysicalDeviceQueueFamilyCount != 0);

	LOG(kGraphics, kInfo, "Physical device queues ({}):", uiPhysicalDeviceQueueFamilyCount);
	for (int64_t i = 0; i < uiPhysicalDeviceQueueFamilyCount; ++i)
	{
		VkBool32 supportsPresentVkBool32 = VK_FALSE;
		CHECK_VK(vkGetPhysicalDeviceSurfaceSupportKHR(mVkPhysicalDevice, static_cast<uint32_t>(i), mVkSurfaceKHR, &supportsPresentVkBool32));
		LOG(kGraphics, kInfo, "  {} | {} | {} | {}", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 ? "VK_QUEUE_GRAPHICS_BIT" : "                     ", supportsPresentVkBool32 == VK_TRUE ? "Supports present" : "                ", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 ? "VK_QUEUE_COMPUTE_BIT" : "                    ", (mVkQueueFamilyProperties.at(i).queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 ? "VK_QUEUE_TRANSFER_BIT" : "                     ");
	}
	LOG(kGraphics, kInfo, "");

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
	LOG(kGraphics, kInfo, "Selected queue families: graphics {}, present {}, transfer {}", miGraphicsQueueFamilyIndex, miPresentQueueFamilyIndex, miTransferQueueFamilyIndex);

	mTransferImageGranularity = mVkQueueFamilyProperties.at(miTransferQueueFamilyIndex).minImageTransferGranularity;
	LOG(kGraphics, kDebug, "Transfer queue minImageTransferGranularity: ({}, {}, {})", mTransferImageGranularity.width, mTransferImageGranularity.height, mTransferImageGranularity.depth);
}

void InstanceManager::SelectSurfaceFormat()
{
	uint32_t uiFormatCount = 0;
	CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurfaceKHR, &uiFormatCount, nullptr));
	ASSERT(uiFormatCount != 0);
	std::vector<VkSurfaceFormatKHR> physicalDeviceSurfaceFormats(uiFormatCount);
	CHECK_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(mVkPhysicalDevice, mVkSurfaceKHR, &uiFormatCount, physicalDeviceSurfaceFormats.data()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	LOG(kGraphics, kInfo, "Surface formats ({}):", physicalDeviceSurfaceFormats.size());
	for ([[maybe_unused]] const VkSurfaceFormatKHR& rVkSurfaceFormatKHR : physicalDeviceSurfaceFormats)
	{
		common::ScopedWorkbufferAllocation<const char*> pcFormat = gEnumToString.Convert(rVkSurfaceFormatKHR.format, rWorkbuffer);
		common::ScopedWorkbufferAllocation<const char*> pcColorSpace = gEnumToString.Convert(rVkSurfaceFormatKHR.colorSpace, rWorkbuffer);
		LOG(kGraphics, kInfo, "  {} ({})", pcFormat, pcColorSpace);
	}
	LOG(kGraphics, kInfo, "");

	// If the format list includes just one entry of VK_FORMAT_UNDEFINED, the surface has no preferred format
	if (uiFormatCount == 1 && physicalDeviceSurfaceFormats.at(0).format == VK_FORMAT_UNDEFINED)
	{
		mFramebufferVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
		{
			common::ScopedWorkbufferAllocation<const char*> pcFormat = gEnumToString.Convert(mFramebufferVkFormat, rWorkbuffer);
			common::ScopedWorkbufferAllocation<const char*> pcColorSpace = gEnumToString.Convert(mFramebufferVkColorSpace, rWorkbuffer);
			LOG(kGraphics, kInfo, "Selected framebuffer format: {} with color space: {} (no preferred format)\n", pcFormat, pcColorSpace);
		}
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
				{
					common::ScopedWorkbufferAllocation<const char*> pcFormat = gEnumToString.Convert(mFramebufferVkFormat, rWorkbuffer);
					common::ScopedWorkbufferAllocation<const char*> pcColorSpace = gEnumToString.Convert(mFramebufferVkColorSpace, rWorkbuffer);
					LOG(kGraphics, kInfo, "Selected framebuffer format: {} with color space: {}\n", pcFormat, pcColorSpace);
				}
				break;
			}
		}

		// Fallback to first available format if preferred formats not found
		if (!bFoundPreferredFormat)
		{
			mFramebufferVkFormat = physicalDeviceSurfaceFormats.at(0).format;
			mFramebufferVkColorSpace = physicalDeviceSurfaceFormats.at(0).colorSpace;
			{
				common::ScopedWorkbufferAllocation<const char*> pcFormat = gEnumToString.Convert(mFramebufferVkFormat, rWorkbuffer);
				common::ScopedWorkbufferAllocation<const char*> pcColorSpace = gEnumToString.Convert(mFramebufferVkColorSpace, rWorkbuffer);
				LOG(kGraphics, kInfo, "Using fallback surface format: {} with color space: {} (preferred formats not available)\n", pcFormat, pcColorSpace);
			}
		}
	}
}

void InstanceManager::SelectDepthFormat()
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Prefer high precision depth formats
	VkFormat pVkFormats[] {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM, VK_FORMAT_D16_UNORM_S8_UINT};

	// Search first for optimal formats
	for (VkFormat& rFormat : pVkFormats)
	{
		VkFormatProperties vkFormatProperties {};
		vkGetPhysicalDeviceFormatProperties(mVkPhysicalDevice, rFormat, &vkFormatProperties);

		if ((vkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
		{
			LOG(kGraphics, kInfo, "Depth format selected: {} (optimal)\n", gEnumToString.Convert(rFormat, rWorkbuffer));
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
				LOG(kGraphics, kInfo, "Depth format selected: {} (linear)\n", gEnumToString.Convert(rFormat, rWorkbuffer));
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

	if constexpr (kbVulkanDebugLayers)
	{
		if (mVkDebugUtilsMessengerEXT != nullptr)
		{
			vkDestroyDebugUtilsMessengerEXT(mVkInstance, mVkDebugUtilsMessengerEXT, nullptr);
		}
	}

	vkDestroyInstance(mVkInstance, nullptr);

	gpInstanceManager = nullptr;
}

void InstanceManager::ReadLayerProperties()
{
	if constexpr (kbVulkanDebugLayers)
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

		LOG(kGraphics, kInfo, "\nFound {} Vulkan validation layers:", uiLayerCount);

		if (uiLayerCount == 0)
		{
			return;
		}

		std::vector<VkLayerProperties> instanceLayerProperties(uiLayerCount);
		CHECK_VK(vkEnumerateInstanceLayerProperties(&uiLayerCount, instanceLayerProperties.data()));

		for (const VkLayerProperties& rVkLayerProperties : instanceLayerProperties)
		{
			LOG(kGraphics, kInfo, "  {} {}.{}", rVkLayerProperties.layerName, VK_VERSION_PATCH(rVkLayerProperties.specVersion), rVkLayerProperties.implementationVersion);

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
				LOG(kGraphics, kInfo, "    Extension: {} {}", rVkExtensionProperties.extensionName, VK_VERSION_PATCH(rVkExtensionProperties.specVersion));
			}
		}

		LOG(kGraphics, kInfo, "");
		for (const char* pcLayer : mValidationLayers)
		{
			LOG(kGraphics, kInfo, "Found \"{}\"", pcLayer);
		}
		LOG(kGraphics, kInfo, "");
	}
}

} // namespace engine
