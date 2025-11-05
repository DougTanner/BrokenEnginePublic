#pragma once

namespace engine
{

class MemoryManager
{
public:

	MemoryManager();
	~MemoryManager();

	VmaAllocator mpAllocator = nullptr;
	VmaVulkanFunctions mVmaFunctions = {};
};

inline MemoryManager* gpMemoryManager = nullptr;

} // namespace engine
