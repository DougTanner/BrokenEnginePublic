#pragma once

namespace engine
{

struct RenderFrame;

class CommandBufferManager
{
public:

	CommandBufferManager();
	~CommandBufferManager();

	void RecordCommandBuffers();
	void RecordCommandBuffer(int64_t iFramebuffer);
	void RecordGlobalCommandBuffer(int64_t iFramebuffer);
	void RecordMainCommandBuffer(int64_t iFramebuffer);

	void SubmitGlobalCommandBuffer(int64_t iFramebufferIndex);
	void SubmitMainCommandBuffer(int64_t iFramebufferIndex, bool bSignalFence);
	void SubmitUiCommandBuffer(int64_t iFramebufferIndex);

	std::vector<CommandBuffers> mPerFramebufferCommandBuffers;

	common::PersistentWorker mSubmitGlobal;
	common::PersistentWorker mSubmitMain;

	// Accepted: Semaphore waits at submission granularity. VkEvent would allow non-particle compute to proceed,
	// but the GPU occupancy gain is minimal given the current workload mix.
	VkSemaphore mParticleSyncVkSemaphore = VK_NULL_HANDLE;
	bool mbParticleSemaphoreSignaled = false;

	bool mbSaveScreenshot = false;

private:

	void RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);
	void RecordWindSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiWindWidth, Pipeline* pPipelines);
	void RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, Pipeline* pPipelines);
	void RecordParticleUpdatePasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines);

	void RecordLightingBlurMRT(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, int64_t iLevel, float fBlurDivisor, float fDistanceCount, VkExtent3D previousExtent);

	void SubmitGlobalToQueue(int64_t iFramebufferIndex);
	void SubmitMainToQueue(int64_t iFramebufferIndex, bool bSignalFence);
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
