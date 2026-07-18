#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class CommandBufferManager
{
public:

	CommandBufferManager();
	~CommandBufferManager();

	void RecordCommandBuffers();
	void RecordCommandBuffer(int64_t iFramebuffer);

	void SubmitGlobalCommandBuffer(int64_t iFramebufferIndex);
	void SubmitMainCommandBuffer(int64_t iFramebufferIndex);
	void SubmitUiCommandBuffer(int64_t iFramebufferIndex);

	std::vector<CommandBuffers> mPerFramebufferCommandBuffers;

	common::PersistentWorker mSubmitGlobal;
	common::PersistentWorker mSubmitMain;

	// Accepted: Semaphore waits at submission granularity. VkEvent would allow non-particle compute to proceed,
	// but the GPU occupancy gain is minimal given the current workload mix.
	VkSemaphore mParticleSyncVkSemaphore = VK_NULL_HANDLE;
	bool mbParticleSemaphoreSignaled = false;

private:

	void SubmitGlobalToQueue(int64_t iFramebufferIndex);
	void SubmitMainToQueue(int64_t iFramebufferIndex);
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
