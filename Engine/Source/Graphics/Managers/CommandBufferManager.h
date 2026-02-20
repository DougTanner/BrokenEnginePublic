#pragma once

#include "PersistentWorker.h"

#include "Graphics/Objects/CommandBuffers.h"

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

	// DT: TODO Semaphore waits at submission granularity, stalling all compute in the global CB until the previous
	// frame's main CB completes. A VkEvent (via VK_KHR_synchronization2 vkCmdSetEvent2/vkCmdWaitEvents2) would
	// allow finer-grained synchronization without stalling non-particle compute work.
	VkSemaphore mParticleSyncVkSemaphore = VK_NULL_HANDLE;
	bool mbParticleSemaphoreSignaled = false;

	bool mbSaveScreenshot = false;

private:

	void SubmitGlobalCommandBufferImpl(int64_t iFramebufferIndex);
	void SubmitMainCommandBufferImpl(int64_t iFramebufferIndex, bool bSignalFence);
};

inline CommandBufferManager* gpCommandBufferManager = nullptr;

} // namespace engine
