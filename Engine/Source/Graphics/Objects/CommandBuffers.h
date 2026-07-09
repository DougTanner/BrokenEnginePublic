#pragma once

#if defined(BT_CLIENT)

namespace engine
{

enum class CommandBufferFlags : uint8_t
{
	kRecorded = 0x01,
	kExecuted = 0x02,
};
using CommandBufferFlags_t = common::Flags<CommandBufferFlags>;

class CommandBuffers
{
public:

	CommandBuffers() = delete;
	CommandBuffers(int64_t iFramebuffer);
	CommandBuffers(const CommandBuffers&) = delete;
	CommandBuffers& operator=(const CommandBuffers&) = delete;
	CommandBuffers(CommandBuffers&& rOther) noexcept;
	~CommandBuffers();

	int64_t miFramebuffer = 0;
	// mFlags / mVkFence are non-atomic by design — three writers (kExecuted set on the mSubmitGlobal worker
	// in CommandBufferManager::SubmitGlobalToQueue, mVkFence reset on mSubmitMain in SubmitMainToQueue, read
	// back on the main thread in Graphics) are sequenced only by the PersistentWorker Wake/Wait chain. Not
	// thread-safe for new callers outside that chain. Same "plain member published across a PersistentWorker
	// Wake/Wait edge" family as CommandBufferManager's mbParticleSemaphoreSignaled, SwapchainManager::PresentToQueue
	// (meDestroyType), and TextureManager's mbHasPendingAcquireBarriers.
	CommandBufferFlags_t mFlags;
	VkCommandPool mVkCommandPool = VK_NULL_HANDLE;
	VkCommandBuffer mGlobalVkCommandBuffer = VK_NULL_HANDLE;
	VkCommandBuffer mMainVkCommandBuffer = VK_NULL_HANDLE;

	VkSemaphore mGlobalFinishedVkSemaphore = VK_NULL_HANDLE;
	VkSemaphore mMainFinishedVkSemaphore = VK_NULL_HANDLE;

	VkCommandBuffer mImGuiVkCommandBuffer = VK_NULL_HANDLE;
	VkSemaphore mImGuiFinishedVkSemaphore = VK_NULL_HANDLE;

	VkFence mVkFence = VK_NULL_HANDLE;
};

} // namespace engine

#endif // defined(BT_CLIENT)
