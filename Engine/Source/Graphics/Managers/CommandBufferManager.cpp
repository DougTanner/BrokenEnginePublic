#if defined(BT_CLIENT)

#include "CommandBufferManager.h"

#include "CommandBufferRecordGlobal.h"
#include "CommandBufferRecordMain.h"
#include "Profile/ProfileManager.h"

namespace engine
{

CommandBufferManager::CommandBufferManager()
: mSubmitGlobal(common::kThreadSubmitGlobal)
, mSubmitMain(common::kThreadSubmitMain)
{
	ASSERT(gpCommandBufferManager == nullptr);

	gpCommandBufferManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerCommandBufferManager);

	// Because one of the drawing commands involves binding the right VkFramebuffer, we'll actually have to record a command buffer for every image in the swap chain
	mPerFramebufferCommandBuffers.reserve(gpSwapchainManager->mFramebuffers.size());
	for (int64_t i = 0; i < static_cast<int64_t>(gpSwapchainManager->mFramebuffers.size()); ++i)
	{
		mPerFramebufferCommandBuffers.emplace_back(i);
	}

	VkSemaphoreCreateInfo vkSemaphoreCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
	};
	CHECK_VK(vkCreateSemaphore(gpDeviceManager->mVkDevice, &vkSemaphoreCreateInfo, nullptr, &mParticleSyncVkSemaphore));
}

CommandBufferManager::~CommandBufferManager()
{
	vkDestroySemaphore(gpDeviceManager->mVkDevice, mParticleSyncVkSemaphore, nullptr);

	if (gpCommandBufferManager == this)
	{
		gpCommandBufferManager = nullptr;
	}
}

void CommandBufferManager::RecordCommandBuffers()
{
	ScopedBootTimer scopedBootTimer(kBootTimerRecordCommandBuffers);

	for (int64_t i = 0; i < static_cast<int64_t>(mPerFramebufferCommandBuffers.size()); ++i)
	{
		RecordCommandBuffer(i);
	}
}

void CommandBufferManager::RecordCommandBuffer(int64_t iFramebuffer)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebuffer);

	// Guard: Command buffers are immutable after initial recording, only re-recorded when CommandBufferManager is destroyed/recreated (resize, device lost).
	if (rCommandBuffers.mFlags & CommandBufferFlags::kRecorded)
	{
		return;
	}
	rCommandBuffers.mFlags.Set(CommandBufferFlags::kRecorded);
	rCommandBuffers.mFlags.Clear(CommandBufferFlags::kExecuted);

	LOG(kGraphics, kDebug, "Record command buffer: {}", iFramebuffer);

	CommandBufferRecordGlobal::Record(iFramebuffer);
	CommandBufferRecordMain::Record(iFramebuffer);
}

void CommandBufferManager::SubmitGlobalToQueue(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	// Prepend acquire barrier command buffer for QFOT when textures were adopted this frame. mbHasPendingAcquireBarriers
	// and miAcquireFramebufferIndex are written on the main thread in TextureManager::ProcessPendingTextures; under
	// kbRenderThread this read runs on the mSubmitGlobal worker, safe only because SubmitGlobalCommandBuffer's
	// mSubmitGlobal.Wake() edge published those writes first. Same family as CommandBuffers.h (mFlags/mVkFence).
	VkCommandBuffer pCommandBuffers[2] {};
	uint32_t uiCommandBufferCount = 0;
	if (gpTextureManager->mbHasPendingAcquireBarriers)
	{
		pCommandBuffers[uiCommandBufferCount++] = gpTextureManager->mAcquireVkCommandBuffers.at(gpTextureManager->miAcquireFramebufferIndex);
	}
	pCommandBuffers[uiCommandBufferCount++] = rCommandBuffers.mGlobalVkCommandBuffer;

	uint32_t uiWaitSemaphoreCount = 0;
	VkSemaphore pWaitSemaphores[1] {};
	VkPipelineStageFlags pWaitDstStageMask[1] {};
	if (mbParticleSemaphoreSignaled)
	{
		pWaitSemaphores[uiWaitSemaphoreCount] = mParticleSyncVkSemaphore;
		pWaitDstStageMask[uiWaitSemaphoreCount] = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		++uiWaitSemaphoreCount;
	}

	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = uiWaitSemaphoreCount,
		.pWaitSemaphores = pWaitSemaphores,
		.pWaitDstStageMask = pWaitDstStageMask,
		.commandBufferCount = uiCommandBufferCount,
		.pCommandBuffers = pCommandBuffers,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &rCommandBuffers.mGlobalFinishedVkSemaphore,
	};

	gpProfileManager->CpuStop(kCpuTimerAcquireToGlobal, {CpuStopFlags::kSmoothNow, CpuStopFlags::kCrossThread});

	gpProfileManager->CpuStart(kCpuTimerSubmitGlobal);
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitGlobal);

	rCommandBuffers.mFlags.Set(CommandBufferFlags::kExecuted);
}

void CommandBufferManager::SubmitGlobalCommandBuffer(int64_t iFramebufferIndex)
{
	if constexpr (kbRenderThread)
	{
		mSubmitGlobal.Wake([this, iFramebufferIndex]()
		{
			SubmitGlobalToQueue(iFramebufferIndex);
		});
	}
	else
	{
		SubmitGlobalToQueue(iFramebufferIndex);
	}
}

void CommandBufferManager::SubmitMainToQueue(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	VkSemaphore vkSemaphores[]
	{
		rCommandBuffers.mGlobalFinishedVkSemaphore,
		gpSwapchainManager->mImageAvailableVkSemaphore,
	};
	VkPipelineStageFlags vkPipelineStageFlags[]
	{
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	VkSemaphore vkSignalSemaphores[]
	{
		rCommandBuffers.mMainFinishedVkSemaphore,
		mParticleSyncVkSemaphore,
	};

	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = static_cast<uint32_t>(std::size(vkSemaphores)),
		.pWaitSemaphores = vkSemaphores,
		.pWaitDstStageMask = vkPipelineStageFlags,
		.commandBufferCount = 1,
		.pCommandBuffers = &rCommandBuffers.mMainVkCommandBuffer,
		.signalSemaphoreCount = static_cast<uint32_t>(std::size(vkSignalSemaphores)),
		.pSignalSemaphores = vkSignalSemaphores,
	};
	gpProfileManager->CpuStart(kCpuTimerSubmitImage);
	// Fence reset/signal split: Main resets mVkFence but submits with VK_NULL_HANDLE.
	// The fence is (re-)signaled by the inseparable following UI submit (ImGuiManager::Submit submits with
	// mVkFence); SubmitMainCommandBuffer is always followed by SubmitUiCommandBuffer (Graphics.cpp). Keep
	// that pairing intact — Main resets the fence here, the following UI submit signals it.
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence));
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitImage);

	// mParticleSyncVkSemaphore (signaled above via vkSignalSemaphores) is waited by the NEXT frame's
	// SubmitGlobalToQueue, gated on mbParticleSemaphoreSignaled — a cross-frame, cross-method pairing that
	// holds only because a Main submit always precedes the next Global submit. Same "plain member published across
	// a PersistentWorker Wake/Wait edge" family as SwapchainManager::PresentToQueue (meDestroyType) and CommandBuffers.h
	// (mFlags/mVkFence).
	mbParticleSemaphoreSignaled = true;
}

void CommandBufferManager::SubmitMainCommandBuffer(int64_t iFramebufferIndex)
{
	if constexpr (kbRenderThread)
	{
		mSubmitMain.Wake([this, iFramebufferIndex]()
		{
			mSubmitGlobal.Wait();
			SubmitMainToQueue(iFramebufferIndex);
		});
	}
	else
	{
		SubmitMainToQueue(iFramebufferIndex);
	}
}

void CommandBufferManager::SubmitUiCommandBuffer(int64_t iFramebufferIndex)
{
	mSubmitMain.Wait();

	gpImGuiManager->Submit(iFramebufferIndex);
}

} // namespace engine

#endif // defined(BT_CLIENT)
