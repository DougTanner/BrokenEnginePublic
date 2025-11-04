#include "MemoryManager.h"

#pragma warning(push, 0)
#pragma warning(disable : 4100)
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#pragma warning(pop)

#include "Graphics/Graphics.h"

namespace engine
{

MemoryManager::MemoryManager()
{
	gpMemoryManager = this;

	// Configure VMA allocator
	VmaAllocatorCreateInfo allocatorCreateInfo = {};
	allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
	allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
	allocatorCreateInfo.physicalDevice = gpInstanceManager->mVkPhysicalDevice;
	allocatorCreateInfo.device = gpDeviceManager->mVkDevice;
	allocatorCreateInfo.instance = gpInstanceManager->mVkInstance;

	CHECK_VK(vmaCreateAllocator(&allocatorCreateInfo, &mpAllocator));
}

MemoryManager::~MemoryManager()
{
	vmaDestroyAllocator(mpAllocator);
	mpAllocator = nullptr;

	gpMemoryManager = nullptr;
}

}
