#include "CommandBufferManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/Screenshot.h"
#include "Profile/ProfileManager.h"

#include "Frame/Render.h"

namespace engine
{

using enum TextureLayout;

CommandBufferManager::CommandBufferManager()
{
	gpCommandBufferManager = this;

	SCOPED_BOOT_TIMER(kBootTimerCommandBufferManager);

	// Because one of the drawing commands involves binding the right VkFramebuffer, we'll actually have to record a command buffer for every image in the swap chain
	mPerFramebufferCommandBuffers.reserve(gpSwapchainManager->mFramebuffers.size());
	for (int64_t i = 0; i < gpSwapchainManager->mFramebuffers.size(); ++i)
	{
		mPerFramebufferCommandBuffers.emplace_back(i);
	}
}

CommandBufferManager::~CommandBufferManager()
{
	gpCommandBufferManager = nullptr;
}

void CommandBufferManager::RecordCommandBuffers()
{
	SCOPED_BOOT_TIMER(kBootTimerRecordCommandBuffers);

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
	rCommandBuffers.mFlags |= CommandBufferFlags::kRecorded;
	rCommandBuffers.mFlags.Clear(CommandBufferFlags::kExecuted);

	LOG("Record command buffer: {}", iFramebuffer);

	RecordGlobalCommandBuffer(iFramebuffer);
	RecordImageCommandBuffer(iFramebuffer);
}

void CommandBufferManager::RecordGlobalCommandBuffer(int64_t iFramebuffer)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebuffer);
	Pipeline* pPipelines = gpPipelineManager->mpPipelines;
	int64_t iCommandBuffer = iFramebuffer;

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = 0,
		.pInheritanceInfo = nullptr,
	};

	VkCommandBuffer vkCommandBuffer = rCommandBuffers.mGlobalVkCommandBuffer;
	CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
	PROFILE_MANAGER_RESET_GLOBAL_QUERY_POOLS(iCommandBuffer, vkCommandBuffer);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

#if defined(ENABLE_DEBUG_PRINTF_EXT)
	gpTextureManager->mLogTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineLog].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(iCommandBuffer), 0.0f, 0.0f, 0.0f});
	gpTextureManager->mLogTexture.RecordEndRenderPass(vkCommandBuffer);
#endif

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);
#if 0
	pPipelines[kPipelineLongParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
	pPipelines[kPipelineSquareParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
#endif
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
	gpTextureManager->mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0, {1.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, gpTextureManager->mShadowTexture.mInfo.extent.width, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlur].RecordCompute(iCommandBuffer, vkCommandBuffer, 1, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);
	gpTextureManager->mTerrainColorTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainColor].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainColorTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);
	gpTextureManager->mTerrainNormalTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainNormal].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainNormalTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
	gpTextureManager->mTerrainAmbientOcclusionTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainAmbientOcclusion].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainAmbientOcclusionTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);
	gpTextureManager->mSmokeTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadTwo].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearTwo].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mSmokeTextureTwo.RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadOne].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearOne].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

	Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesRender].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesLighting].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesRender].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesLighting].mIndirectVkBuffer},
	}));

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);
#if 0
	pPipelines[kPipelineLongParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
#endif
	Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
	}));
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
#if 0
	pPipelines[kPipelineSquareParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
#endif
	Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
	}));
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);

	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferManager::RecordImageCommandBuffer(int64_t iFramebuffer)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebuffer);
	Pipeline* pPipelines = gpPipelineManager->mpPipelines;
	int64_t iCommandBuffer = iFramebuffer;

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = 0,
		.pInheritanceInfo = nullptr,
	};

	VkCommandBuffer vkCommandBuffer = rCommandBuffers.mImageVkCommandBuffer;
	CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
	PROFILE_MANAGER_RESET_IMAGE_QUERY_POOLS(iCommandBuffer, vkCommandBuffer);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
	gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);
	VkClearValue pClearValues[3] {};
	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = gpTextureManager->mLightingVkRenderPass,
		.framebuffer = gpTextureManager->mLightingVkFramebuffer,
		.renderArea = {.offset = {0, 0}, .extent = {static_cast<uint32_t>(gpTextureManager->mpLightingTextures[0].mInfo.extent.width), static_cast<uint32_t>(gpTextureManager->mpLightingTextures[0].mInfo.extent.height)}},
		.clearValueCount = 3,
		.pClearValues = pClearValues,
	};
	vkCmdBeginRenderPass(vkCommandBuffer, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesLightingMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	vkCmdEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);

	// Lighting blur passes
	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingBlur);
	float fBlurFirstDivisor = gLightingBlurFirstDivisor.Get();
	if (gpGraphics->mFramebufferExtent2D.height <= 1080)
	{
		fBlurFirstDivisor *= 0.5f;
	}
	float fBlurDivisor = gLightingBlurDivisor.Get();
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mpLightingTextures[0].mInfo.extent : gpTextureManager->mpRedLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mpRedLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mpRedLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpRedLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mpRedLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mpLightingTextures[1].mInfo.extent : gpTextureManager->mpGreenLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mpGreenLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mpGreenLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpGreenLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mpGreenLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mpLightingTextures[2].mInfo.extent : gpTextureManager->mpBlueLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mpBlueLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mpBlueLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpBlueLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mpBlueLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingBlur);

	// Lighting combine passes
	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	gpTextureManager->mpRedLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mpRedLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineRedLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mpRedLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mpGreenLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mpGreenLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineGreenLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mpGreenLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mpBlueLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mpBlueLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineBlueLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mpBlueLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Smoke emit pass
	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);
	gpTextureManager->mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
#if 0
	pPipelines[kPipelineSmokePuffs].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	pPipelines[kPipelineSmokeTrails].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
#endif
	gpTextureManager->mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mObjectShadowsTexture.mVkFramebuffer, {gpTextureManager->mObjectShadowsTexture.mInfo.extent.width, gpTextureManager->mObjectShadowsTexture.mInfo.extent.height}, gpTextureManager->mObjectShadowsTexture.mInfo.renderPassVkClearColorValue, false, false, true, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicGltfPipelineShadowMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);

	// Object shadows blur pass
	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
	gpTextureManager->mObjectShadowsBlurTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineObjectShadowsBlur].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mObjectShadowsBlurTexture.RecordEndRenderPass(vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);

	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, true, gMultisampling.Get<bool>(), true, VK_SUBPASS_CONTENTS_INLINE);

	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicGltfPipelineMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);
	pPipelines[kPipelineTerrain].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);
	pPipelines[kPipelineWater].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {0.0f, 0.0f, 0.0f, 0.0f});
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);
#if 0
	pPipelines[kPipelineLongParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);
#if 0
	pPipelines[kPipelineSquareParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
#endif
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);

	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesVisibleLightsMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerWidgets);
	pPipelines[kPipelineWidgets].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerWidgets);

	GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerText);
	pPipelines[kPipelineProfileText].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerText);

	Texture::RecordEndRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass);
	GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferManager::SubmitGlobalCommandBuffer(int64_t iFramebufferIndex)
{
#if defined(ENABLE_RENDER_THREAD)
	mSubmitGlobal = std::async(std::launch::async, [this, iFramebufferIndex]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
		CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);

		std::vector<VkSemaphore> vkSemaphores;
		std::vector<VkPipelineStageFlags> vkPipelineStageFlags;

		VkSubmitInfo vkSubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = static_cast<uint32_t>(vkSemaphores.size()),
			.pWaitSemaphores = vkSemaphores.data(),
			.pWaitDstStageMask = vkPipelineStageFlags.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &rCommandBuffers.mGlobalVkCommandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rCommandBuffers.mGlobalFinishedVkSemaphore,
		};

		CPU_PROFILE_STOP_AND_SMOOTH(kCpuTimerAcquireToGlobal);

		CPU_PROFILE_START(kCpuTimerSubmitGlobal);
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
		CPU_PROFILE_STOP(kCpuTimerSubmitGlobal);

		rCommandBuffers.mFlags |= CommandBufferFlags::kExecuted;
#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

void CommandBufferManager::SubmitImageCommandBuffer(int64_t iFramebufferIndex)
{
#if defined(ENABLE_RENDER_THREAD)
	mSubmitImage = std::async(std::launch::async, [this, iFramebufferIndex]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

		CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);

		std::vector<VkSemaphore> vkSemaphores;
		std::vector<VkPipelineStageFlags> vkPipelineStageFlags;
		vkSemaphores.emplace_back(rCommandBuffers.mGlobalFinishedVkSemaphore);
		vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
		vkSemaphores.emplace_back(gpSwapchainManager->mImageAvailableVkSemaphore);
		vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	#if defined(ENABLE_RENDER_THREAD)
		mSubmitGlobal.get();
	#endif

		VkSubmitInfo vkSubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = static_cast<uint32_t>(vkSemaphores.size()),
			.pWaitSemaphores = vkSemaphores.data(),
			.pWaitDstStageMask = vkPipelineStageFlags.data(),
			.commandBufferCount = 1,
			.pCommandBuffers = &rCommandBuffers.mImageVkCommandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rCommandBuffers.mImageFinishedVkSemaphore,
		};
		CPU_PROFILE_START(kCpuTimerSubmitImage);
		CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence));
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, rCommandBuffers.mVkFence));
		CPU_PROFILE_STOP(kCpuTimerSubmitImage);

	#if defined(ENABLE_SCREENSHOTS)
		if (mbSaveScreenshot)
		{
			mbSaveScreenshot = false;
			SaveScreenshot(iFramebufferIndex);
		}
	#endif
	
#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

} // namespace engine
