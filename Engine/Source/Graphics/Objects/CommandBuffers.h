#pragma once

namespace engine
{

inline constexpr int64_t kiCommandBuffersPerFramebuffer = 2;

class CommandBuffers
{
public:

	CommandBuffers();
	~CommandBuffers();

	// Cycle between double-buffered slots (0 → 1 → 0...) to enable parallel GPU/CPU work
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
	VkCommandBuffer mpImageCommandBuffers[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpPlayerSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpSpaceshipsSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpPlayerMissilesSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpAreaLightsSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpPointLightsSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpHexShieldsLightingSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpLongParticlesLightingSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpSquareParticlesLightingSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};
	VkCommandBuffer mpSceneSecondaryBuffer[kiCommandBuffersPerFramebuffer] {};

	VkSemaphore mpGlobalFinishedVkSemaphores[kiCommandBuffersPerFramebuffer] {};
	VkSemaphore mpImageFinishedVkSemaphores[kiCommandBuffersPerFramebuffer] {};

	VkFence mpVkFences[kiCommandBuffersPerFramebuffer] {};
};

} // namespace engine
