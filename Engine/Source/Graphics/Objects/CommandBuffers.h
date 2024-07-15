#pragma once

namespace engine
{

inline constexpr int64_t kiCommandBuffersPerFramebuffer = 2;

class CommandBuffers
{
public:

	CommandBuffers();
	~CommandBuffers();

	void Next()
	{
		++miCurrentIndex;
		if (miCurrentIndex == kiCommandBuffersPerFramebuffer)
		{
			miCurrentIndex = 0;
		}
	}

	int64_t miCurrentIndex = 0;

	bool mpbRecorded[kiCommandBuffersPerFramebuffer] {};
	bool mpbExecuted[kiCommandBuffersPerFramebuffer] {};
	VkCommandPool mpCommandPools[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpGlobalCommandBuffers[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpMainCommandBuffers[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpImageCommandBuffers[kiCommandBuffersPerFramebuffer] {};

	VkSemaphore mpGlobalFinishedVkSemaphores[kiCommandBuffersPerFramebuffer] {};
	VkSemaphore mpMainFinishedVkSemaphores[kiCommandBuffersPerFramebuffer] {};
	VkSemaphore mpImageFinishedVkSemaphores[kiCommandBuffersPerFramebuffer] {};

	VkFence mpVkFences[kiCommandBuffersPerFramebuffer] {};
};

} // namespace engine
