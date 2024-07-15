#pragma once

namespace engine
{

inline constexpr std::chrono::nanoseconds kFenceTimeoutNs = 1'000'000'000ns;

int64_t FindMemoryType(int64_t iTypeFilter, VkMemoryPropertyFlags properties);

class DeviceManager
{
public:

	DeviceManager();
	~DeviceManager();

	VkDevice mVkDevice = VK_NULL_HANDLE;

	VkQueue mGraphicsVkQueue = VK_NULL_HANDLE;
	VkQueue mPresentVkQueue = VK_NULL_HANDLE;

	VkDescriptorPool mVkDescriptorPool = VK_NULL_HANDLE;

private:

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	VkDebugReportCallbackEXT mVkDebugReportCallbackEXT = nullptr;
#endif
};

inline DeviceManager* gpDeviceManager = nullptr;

} // namespace engine
