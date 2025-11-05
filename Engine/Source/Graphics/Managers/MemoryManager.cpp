#include "MemoryManager.h"

#pragma warning(push, 0)
#pragma warning(disable : 4100 6326 6386 6387)
#define VMA_IMPLEMENTATION
// Define VMA Vulkan version to match runtime configuration
#if defined(ENABLE_VULKAN_1_2)
	#define VMA_VULKAN_VERSION 1002000 // 1.2.0
#else
	#define VMA_VULKAN_VERSION 1001000 // 1.1.0
#endif
#include <vma/vk_mem_alloc.h>
#pragma warning(pop)

#include "Graphics/Graphics.h"

namespace engine
{

MemoryManager::MemoryManager()
{
	gpMemoryManager = this;

	// Configure VMA function pointers (VMA will fetch all other functions automatically)
	mVmaFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
	mVmaFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

	VmaAllocatorCreateInfo allocatorCreateInfo =
	{
		.flags = gpDeviceManager->mbMemoryBudgetAvailable ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : static_cast<VmaAllocatorCreateFlags>(0),
		.physicalDevice = gpInstanceManager->mVkPhysicalDevice,
		.device = gpDeviceManager->mVkDevice,
		.pVulkanFunctions = &mVmaFunctions,
		.instance = gpInstanceManager->mVkInstance,
	#if defined(ENABLE_VULKAN_1_2)
		.vulkanApiVersion = VK_API_VERSION_1_2,
	#else
		.vulkanApiVersion = VK_API_VERSION_1_1,
	#endif
	};

	CHECK_VK(vmaCreateAllocator(&allocatorCreateInfo, &mpAllocator));
}

MemoryManager::~MemoryManager()
{
	vmaDestroyAllocator(mpAllocator);
	mpAllocator = nullptr;

	gpMemoryManager = nullptr;
}

}
