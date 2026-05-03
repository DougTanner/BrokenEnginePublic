#pragma once

namespace engine
{

inline constexpr std::chrono::nanoseconds kFenceTimeoutNanoseconds = 4'000'000'000ns;

int64_t FindMemoryType(int64_t iTypeFilter, VkMemoryPropertyFlags vkMemoryPropertyFlags);

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
	VkPipelineCache mVkPipelineCache = VK_NULL_HANDLE;

	bool mbMemoryBudgetAvailable = false;
	bool mbTransferQueueFamilyOwnershipTransferOptional = false;
	bool mbSmoothLinesEnabled = false;
	bool mbWideLinesEnabled = false;

	VkCommandPool mOneShotVkCommandPool = VK_NULL_HANDLE;
	VkFence mOneShotVkFence = VK_NULL_HANDLE;

	VmaAllocator mpAllocator = nullptr;
	VmaVulkanFunctions mVmaFunctions = {};
};

inline DeviceManager* gpDeviceManager = nullptr;

} // namespace engine
