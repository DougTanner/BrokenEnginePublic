#include "CommandBufferManager.h"

#include "Profile/ProfileManager.h"

namespace engine
{

using enum TextureLayout;

CommandBufferManager::CommandBufferManager()
: mSubmitGlobal(common::kThreadSubmitGlobal)
, mSubmitMain(common::kThreadSubmitMain)
{
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

	gpCommandBufferManager = nullptr;
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

	Log("Record command buffer: {}", iFramebuffer);

	RecordGlobalCommandBuffer(iFramebuffer);
	RecordMainCommandBuffer(iFramebuffer);
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
	gpProfileManager->ResetGlobalQueryPools(iCommandBuffer, vkCommandBuffer);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);
	pPipelines[kPipelineLongParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
	pPipelines[kPipelineSquareParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0, {1.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlur].RecordCompute(iCommandBuffer, vkCommandBuffer, 1, gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);
	gpTextureManager->mRenderTargetTextures.mTerrainColorTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainColor].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mRenderTargetTextures.mTerrainColorTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);
	gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainNormal].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
	gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainAmbientOcclusion].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);

	// Wind spread passes (ping-pong: one set active per frame via indirect instance counts)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);

	// Wind spread pass A (writes TextureOne, reads TextureTwo)
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineWindSpreadA].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineWindClearA].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordEndRenderPass(vkCommandBuffer);

	// Wind spread pass B (writes TextureTwo, reads TextureOne)
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineWindSpreadB].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineWindClearB].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordEndRenderPass(vkCommandBuffer);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);

	// Smoke spread passes
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadB].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearB].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadA].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearA].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

	BarrierInfo pBarriers[] =
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesRender].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesLighting].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesRender].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesLighting].mIndirectVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pBarriers, std::size(pBarriers));

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);
	pPipelines[kPipelineLongParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	BarrierInfo pLongParticleBarrier[] =
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pLongParticleBarrier, std::size(pLongParticleBarrier));
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
	pPipelines[kPipelineSquareParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	BarrierInfo pSquareParticleBarrier[] =
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pSquareParticleBarrier, std::size(pSquareParticleBarrier));
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferManager::RecordMainCommandBuffer(int64_t iFramebuffer)
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

	VkCommandBuffer vkCommandBuffer = rCommandBuffers.mMainVkCommandBuffer;
	CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
	gpProfileManager->ResetMainQueryPools(iCommandBuffer, vkCommandBuffer);

	if constexpr (kbEnableDebugPrintf)
	{
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineLog].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(iCommandBuffer), 0.0f, 0.0f, 0.0f});
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordEndRenderPass(vkCommandBuffer);
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
	gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);
	VkClearValue pClearValues[3] {};
	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.framebuffer = gpTextureManager->mRenderTargetTextures.mLightingVkFramebuffer,
		.renderArea = {.offset = {0, 0}, .extent = {static_cast<uint32_t>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width), static_cast<uint32_t>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height)}},
		.clearValueCount = 3,
		.pClearValues = pClearValues,
	};
	vkCmdBeginRenderPass(vkCommandBuffer, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineAxisAlignedLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineHexShieldsLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	pPipelines[kPipelineLongParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	pPipelines[kPipelineSquareParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	vkCmdEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);

	// Lighting blur passes
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingBlur);
	float fBlurFirstDivisor = gLightingBlurFirstDivisor.Get();
	if (gpGraphics->mFramebufferExtent2D.height <= 1080)
	{
		fBlurFirstDivisor *= 0.5f;
	}
	float fBlurDivisor = gLightingBlurDivisor.Get();
	for (int64_t i = 0; i < gpTextureManager->mRenderTargetTextures.miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent : gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpRedLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	for (int64_t i = 0; i < gpTextureManager->mRenderTargetTextures.miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mRenderTargetTextures.mpLightingTextures[1].mInfo.extent : gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpGreenLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	for (int64_t i = 0; i < gpTextureManager->mRenderTargetTextures.miLightingBlurCount; ++i)
	{
		VkExtent3D previousVkExtent3D = i == 0 ? gpTextureManager->mRenderTargetTextures.mpLightingTextures[2].mInfo.extent : gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[i - 1].mInfo.extent;
		gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[i].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
		gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[i].RecordBeginRenderPass(vkCommandBuffer);
		gpPipelineManager->mpBlueLightingBlurPipelines[i].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0,
		{
			static_cast<float>(previousVkExtent3D.width),
			static_cast<float>(previousVkExtent3D.height),
			i == 0 ? fBlurFirstDivisor : fBlurDivisor,
			0.0f,
		});
		gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[i].RecordEndRenderPass(vkCommandBuffer);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingBlur);

	// Lighting combine passes
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineRedLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineGreenLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[iCombineTextureIndex].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[iCombineTextureIndex].RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineBlueLightingCombine].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures[iCombineTextureIndex].RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Smoke emit pass
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmokeAxisAligned])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmoke])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);

	// Wind deposit pass A (writes TextureOne)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWindDeposit);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositA])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordEndRenderPass(vkCommandBuffer);

	// Wind deposit pass B (writes TextureTwo)
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositB])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindDeposit);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkFramebuffer, {gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.height}, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.renderPassVkClearColorValue, false, false, true, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f}, ModelDrawPass::kOpaque);
	}
	gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);

	// Object shadows blur pass
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineObjectShadowsBlur].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, true, gMultisampling.Get<bool>(), true, VK_SUBPASS_CONTENTS_INLINE);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModel])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {}, ModelDrawPass::kOpaque);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);
	pPipelines[kPipelineTerrain].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);
	pPipelines[kPipelineWater].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {0.0f, 0.0f, 0.0f, 0.0f});
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerHexShields);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineHexShields])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerHexShields);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTransparentObjects);
	// Transparent model pass (after all opaque geometry and water for correct blending)
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModel])
	{
		if (pPipeline->mbHasTransparentMaterials)
		{
			pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {}, ModelDrawPass::kTransparent);
		}
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTransparentObjects);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);
	pPipelines[kPipelineLongParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);
	pPipelines[kPipelineSquareParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineBillboards])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerText);
	pPipelines[kPipelineProfileText].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerText);

	Texture::RecordEndRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferManager::SubmitGlobalToQueue(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	// Prepend acquire barrier command buffer for QFOT when textures were adopted this frame
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

	gpProfileManager->CpuStop(kCpuTimerAcquireToGlobal, true, true);

	gpProfileManager->CpuStart(kCpuTimerSubmitGlobal);
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitGlobal, false);

	rCommandBuffers.mFlags.Set(CommandBufferFlags::kExecuted);
}

void CommandBufferManager::SubmitGlobalCommandBuffer(int64_t iFramebufferIndex)
{
	if constexpr (kbEnableRenderThread)
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

void CommandBufferManager::SubmitMainToQueue(int64_t iFramebufferIndex, bool bSignalFence)
{
	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);

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
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence));
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, bSignalFence ? rCommandBuffers.mVkFence : VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitImage, false);

	mbParticleSemaphoreSignaled = true;

	if constexpr (kbEnableScreenshots)
	{
		if (mbSaveScreenshot)
		{
			mbSaveScreenshot = false;
			SaveScreenshot(iFramebufferIndex);
		}
	}
}

void CommandBufferManager::SubmitMainCommandBuffer(int64_t iFramebufferIndex, bool bSignalFence)
{
	if constexpr (kbEnableRenderThread)
	{
		mSubmitMain.Wake([this, iFramebufferIndex, bSignalFence]()
		{
			mSubmitGlobal.Wait();
			SubmitMainToQueue(iFramebufferIndex, bSignalFence);
		});
	}
	else
	{
		SubmitMainToQueue(iFramebufferIndex, bSignalFence);
	}
}

void CommandBufferManager::SubmitUiCommandBuffer(int64_t iFramebufferIndex)
{
	mSubmitMain.Wait();

	gpImGuiManager->Submit(iFramebufferIndex);
}

} // namespace engine
