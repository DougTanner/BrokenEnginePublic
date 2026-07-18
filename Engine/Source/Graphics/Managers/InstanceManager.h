#pragma once

#if defined(BT_CLIENT)

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
		.pNext = kbShaderRealtimeClock ? &mVkPhysicalDeviceShaderClockFeaturesKHR : nullptr,
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
	std::vector<VkQueueFamilyProperties> mVkQueueFamilyProperties;

	// minImageTransferGranularity for the chosen transfer queue family. (1,1,1) for graphics-capable queues; dedicated DMA queues (e.g. NVIDIA) typically report (16,16,8). Consumers must satisfy VUID-vkCmdCopyBufferToImage-imageOffset-07738 against this.
	VkExtent3D mTransferImageGranularity {1, 1, 1};

	VkSurfaceKHR mVkSurfaceKHR = VK_NULL_HANDLE;
	VkSampleCountFlagBits meMaxMultisampleCount = VK_SAMPLE_COUNT_1_BIT;
	VkFormat mFramebufferVkFormat = VK_FORMAT_UNDEFINED;
	VkColorSpaceKHR mFramebufferVkColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	VkFormat mDepthVkFormat = VK_FORMAT_UNDEFINED;

	std::vector<const char*> mValidationLayers;
	VkDebugUtilsMessengerEXT mVkDebugUtilsMessengerEXT = nullptr;

	RENDERDOC_API_1_6_0* mpRenderDocApi = nullptr;

private:

	void ReadLayerProperties();
	void SelectBestPhysicalDevice();
	void ValidatePhysicalDeviceCapabilities();
	void SelectQueueFamilies();
	void SelectSurfaceFormat();
	void SelectDepthFormat();
};

inline InstanceManager* gpInstanceManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
