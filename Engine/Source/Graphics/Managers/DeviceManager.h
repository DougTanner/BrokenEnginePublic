#pragma once

namespace engine
{

inline constexpr std::chrono::nanoseconds kFenceTimeoutNs = 4'000'000'000ns;

int64_t FindMemoryType(int64_t iTypeFilter, VkMemoryPropertyFlags properties);

class DeviceManager
{
public:

	DeviceManager();
	~DeviceManager();

	VkDevice mVkDevice = VK_NULL_HANDLE;

	VkQueue mGraphicsVkQueue = VK_NULL_HANDLE;
	VkQueue mPresentVkQueue = VK_NULL_HANDLE;
	VkQueue mTransferVkQueue = VK_NULL_HANDLE;

	VkDescriptorPool mVkDescriptorPool = VK_NULL_HANDLE;
	VkDescriptorPool mVkDescriptorPoolUpdateAfterBind = VK_NULL_HANDLE;

	bool mbMemoryBudgetAvailable = false;
	bool mbTransferQfotOptional = false;

	VmaAllocator mpAllocator = nullptr;
	VmaVulkanFunctions mVmaFunctions = {};
};

inline DeviceManager* gpDeviceManager = nullptr;

} // namespace engine
