#pragma once

#if defined(BT_CLIENT)

namespace engine
{

inline constexpr std::chrono::nanoseconds kFenceTimeoutNanoseconds = 4'000'000'000ns;

enum class DeviceCapabilityFlags : uint8_t
{
	kMemoryBudgetAvailable                        = 1 << 0,
	kTransferQueueFamilyOwnershipTransferOptional = 1 << 1,
	kSmoothLinesEnabled                           = 1 << 2,
	kWideLinesEnabled                             = 1 << 3,
};

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

	common::Flags<DeviceCapabilityFlags> mCapabilities;

	VkCommandPool mOneShotVkCommandPool = VK_NULL_HANDLE;
	VkFence mOneShotVkFence = VK_NULL_HANDLE;

	VmaAllocator mpAllocator = nullptr;
	VmaVulkanFunctions mVmaFunctions = {};

private:

	void LoadPipelineCache();
	void ProbeTransferQueueOwnershipTransfer(bool bMaintenance9Available);
};

inline DeviceManager* gpDeviceManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
