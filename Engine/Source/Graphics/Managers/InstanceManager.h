#pragma once

namespace engine
{

class InstanceManager
{
public:

	InstanceManager(HINSTANCE hinstance, HWND hwnd);
	~InstanceManager();

	InstanceManager() = delete;

	VkInstance mVkInstance = VK_NULL_HANDLE;

	VkPhysicalDevice mVkPhysicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties mVkPhysicalDeviceMemoryProperties {};

	VkPhysicalDeviceShaderClockFeaturesKHR mVkPhysicalDeviceShaderClockFeaturesKHR =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR,
		.pNext = nullptr,
	};
	VkPhysicalDevice16BitStorageFeatures mVkPhysicalDevice16BitStorageFeatures =
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
#if defined(ENABLE_SHADER_REALTIME_CLOCK_EXT)
		.pNext = &mVkPhysicalDeviceShaderClockFeaturesKHR,
#else
		.pNext = nullptr,
#endif
	};
	VkPhysicalDeviceVulkan12Features mVkPhysicalDeviceVulkan12Features
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &mVkPhysicalDevice16BitStorageFeatures,
	};
	VkPhysicalDeviceFeatures2 mVkPhysicalDeviceFeatures2 = 
	{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &mVkPhysicalDeviceVulkan12Features,
	};

	VkPhysicalDeviceProperties mVkPhysicalDeviceProperties {};

	int64_t miGraphicsQueueFamilyIndex = UINT32_MAX;
	int64_t miPresentQueueFamilyIndex = UINT32_MAX;
	int64_t miTransferQueueFamilyIndex = UINT32_MAX;

	VkSurfaceKHR mVkSurfaceKHR = VK_NULL_HANDLE;
	VkSampleCountFlagBits meMaxMultisampleCount = VK_SAMPLE_COUNT_1_BIT;
	VkFormat mFramebufferVkFormat = VK_FORMAT_UNDEFINED;
	VkFormat mDepthVkFormat = VK_FORMAT_UNDEFINED;

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	void ReadLayerProperties();

	std::vector<const char*> mValidationLayers;
	bool mbFoundKhronosValidation = false;
	VkDebugUtilsMessengerEXT mVkDebugUtilsMessengerEXT = nullptr;
	PFN_vkSetDebugUtilsObjectNameEXT mVkSetDebugUtilsObjectNameEXT = nullptr;
#endif

private:

	HINSTANCE mHinstance = nullptr;
	HWND mHwnd = nullptr;
};

inline InstanceManager* gpInstanceManager = nullptr;

} // namespace engine
