#include "CommandBufferManager.h"

#include "Graphics/Graphics.h"
#include "Graphics/Screenshot.h"
#include "Profile/ProfileManager.h"

#include "Frame/Render.h"
#include "Game.h"

namespace engine
{

using enum TextureLayout;

int64_t CommandBufferManager::CommandBufferCount()
{
	return kiCommandBuffersPerFramebuffer * gpSwapchainManager->mFramebuffers.size();
}

int64_t CommandBufferManager::CommandBufferIndex(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);
	return kiCommandBuffersPerFramebuffer * iFramebufferIndex + rCommandBuffers.miCurrentIndex;
}

CommandBufferManager::CommandBufferManager()
{
	gpCommandBufferManager = this;

	SCOPED_BOOT_TIMER(kBootTimerCommandBufferManager);

	// Because one of the drawing commands involves binding the right VkFramebuffer, we'll actually have to record a command buffer for every image in the swap chain
	mPerFramebufferCommandBuffers.resize(gpSwapchainManager->mFramebuffers.size());
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
		for (int64_t j = 0; j < kiCommandBuffersPerFramebuffer; ++j)
		{
			RecordCommandBuffer(i);
			mPerFramebufferCommandBuffers.at(i).Next();
		}
	}
}

void CommandBufferManager::RecordSecondary(int64_t iFramebuffer, int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, const std::function<void(int64_t, VkCommandBuffer)>& recordCallback)
{
	VkCommandBufferInheritanceInfo vkCommandBufferInheritanceInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
		.pNext = nullptr,
		.renderPass = vkRenderPass,
		.subpass = 0,
		.framebuffer = vkFramebuffer,
		.occlusionQueryEnable = VK_FALSE,
		.queryFlags = 0,
		.pipelineStatistics = 0,
	};

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &vkCommandBufferInheritanceInfo,
	};

	CHECK_VK(vkBeginCommandBuffer(vkSecondaryCommandBuffer, &vkCommandBufferBeginInfo));
	recordCallback(iCommandBuffer, vkSecondaryCommandBuffer);
	CHECK_VK(vkEndCommandBuffer(vkSecondaryCommandBuffer));
}

void CommandBufferManager::RecordCommandBuffer(int64_t iFramebuffer)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebuffer);
	Pipeline* pPipelines = gpPipelineManager->mpPipelines;

	// Guard: Command buffers are immutable after initial recording, only re-recorded when CommandBufferManager is destroyed/recreated (resize, device lost).
	if (rCommandBuffers.mpbRecorded[rCommandBuffers.miCurrentIndex])
	{
		return;
	}
	rCommandBuffers.mpbRecorded[rCommandBuffers.miCurrentIndex] = true;
	rCommandBuffers.mpbExecuted[rCommandBuffers.miCurrentIndex] = false;

	int64_t iCommandBuffer = gpCommandBufferManager->CommandBufferIndex(iFramebuffer);
	LOG("Record command buffer: {} {} -> {}", iFramebuffer, rCommandBuffers.miCurrentIndex, iCommandBuffer);

	// Populate secondary buffer specs for lighting and scene rendering
	mLightingSecondarySpecs =
	{
		{gpTextureManager->mLightingVkRenderPass, gpTextureManager->mLightingVkFramebuffer, rCommandBuffers.mpAreaLightsSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
			gpPipelineManager->mpPipelines[kPipelineAreaLights].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
		}},
		{gpTextureManager->mLightingVkRenderPass, gpTextureManager->mLightingVkFramebuffer, rCommandBuffers.mpPointLightsSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
#if 0
			gpPipelineManager->mpPipelines[kPipelinePointLights].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
#endif
		}},
		{gpTextureManager->mLightingVkRenderPass, gpTextureManager->mLightingVkFramebuffer, rCommandBuffers.mpHexShieldsLightingSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
#if 0
			gpPipelineManager->mpPipelines[kPipelineHexShieldsLighting].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
#endif
		}},
		{gpTextureManager->mLightingVkRenderPass, gpTextureManager->mLightingVkFramebuffer, rCommandBuffers.mpLongParticlesLightingSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
#if 0
			gpPipelineManager->mpPipelines[kPipelineLongParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
#endif
		}},
		{gpTextureManager->mLightingVkRenderPass, gpTextureManager->mLightingVkFramebuffer, rCommandBuffers.mpSquareParticlesLightingSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
#if 0
			gpPipelineManager->mpPipelines[kPipelineSquareParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
#endif
		}},
	};

	mSceneSecondarySpecs =
	{
		{gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, rCommandBuffers.mpPlayerSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
			game::gpGltfPipelines->mpGltfPipelines[game::kGltfPipelinePlayer].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
#if defined(ENABLE_GLTF_TEST)
			game::gpGltfPipelines->mpGltfPipelines[game::kGltfPipelineTest].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
#endif
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
		}},
		{gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, rCommandBuffers.mpSpaceshipsSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
			game::gpGltfPipelines->mpGltfPipelines[game::kGltfPipelineSpaceships].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
		}},
		{gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, rCommandBuffers.mpPlayerMissilesSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
			game::gpGltfPipelines->mpGltfPipelines[game::kGltfPipelinePlayerMissiles].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerObjects);
		}},
		{gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, rCommandBuffers.mpSceneSecondaryBuffer, [](int64_t iCommandBuffer, VkCommandBuffer vkSecondaryCommandBuffer)
		{
			Pipeline* pPipelines = gpPipelineManager->mpPipelines;

			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerTerrain);
			pPipelines[kPipelineTerrain].RecordDraw(iCommandBuffer, vkSecondaryCommandBuffer, 1, 0);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerTerrain);

			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerWater);
			pPipelines[kPipelineWater].RecordDraw(iCommandBuffer, vkSecondaryCommandBuffer, 1, 0, {0.0f, 0.0f, 0.0f, 0.0f});
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerWater);

			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerVisibleLights);
			pPipelines[kPipelineVisibleLights].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerVisibleLights);

			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerWidgets);
			pPipelines[kPipelineWidgets].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerWidgets);

			GPU_PROFILE_START(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerText);
			pPipelines[kPipelineProfileText].RecordDrawIndirect(iCommandBuffer, vkSecondaryCommandBuffer);
			GPU_PROFILE_STOP(iCommandBuffer, vkSecondaryCommandBuffer, kGpuTimerText);
		}},
	};

	// Command buffers recorded once at startup, resubmitted every frame. VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT not used (each recording submitted multiple times).
	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = 0,
		.pInheritanceInfo = nullptr,
	};

	{
		// Global
		VkCommandBuffer vkCommandBuffer = rCommandBuffers.mpGlobalCommandBuffers[rCommandBuffers.miCurrentIndex];
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

	{
		// Image
		VkCommandBuffer vkCommandBuffer = rCommandBuffers.mpImageCommandBuffers[rCommandBuffers.miCurrentIndex];
		CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
		PROFILE_MANAGER_RESET_IMAGE_QUERY_POOLS(iCommandBuffer, vkCommandBuffer);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
		gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

		// Record lighting secondary command buffers - one per lighting pipeline
		for (const auto& spec : mLightingSecondarySpecs)
		{
			RecordSecondary(iFramebuffer, iCommandBuffer, spec.pSecondaryBuffers[rCommandBuffers.miCurrentIndex], spec.vkRenderPass, spec.vkFramebuffer, spec.recordCallback);
		}

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);
		// Single MRT render pass for all 3 lighting channels
		VkClearValue pClearValues[3];
		pClearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
		pClearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
		pClearValues[2].color = {0.0f, 0.0f, 0.0f, 0.0f};
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
		vkCmdBeginRenderPass(vkCommandBuffer, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		// Execute lighting secondary command buffers in order
		for (const auto& spec : mLightingSecondarySpecs)
		{
			vkCmdExecuteCommands(vkCommandBuffer, 1, &spec.pSecondaryBuffers[rCommandBuffers.miCurrentIndex]);
		}

		vkCmdEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);

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
		gpTextureManager->mObjectShadowsTexture.RecordBeginRenderPass(vkCommandBuffer);
		game::gpGltfPipelines->RecordGltfShadowPipelines(iCommandBuffer, vkCommandBuffer);
		gpTextureManager->mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
		gpTextureManager->mObjectShadowsBlurTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineObjectShadowsBlur].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
		gpTextureManager->mObjectShadowsBlurTexture.RecordEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);

		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

		// Record secondary command buffers - one per glTF pipeline type + one grouped for all non-glTF
		for (const auto& spec : mSceneSecondarySpecs)
		{
			RecordSecondary(iFramebuffer, iCommandBuffer, spec.pSecondaryBuffers[rCommandBuffers.miCurrentIndex], spec.vkRenderPass, spec.vkFramebuffer, spec.recordCallback);
		}

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
		Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, true, gMultisampling.Get<bool>(), true, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		// Execute secondary command buffers - glTF pipelines first, then grouped scene rendering
		for (const auto& spec : mSceneSecondarySpecs)
		{
			vkCmdExecuteCommands(vkCommandBuffer, 1, &spec.pSecondaryBuffers[rCommandBuffers.miCurrentIndex]);
		}

		Texture::RecordEndRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

		CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
	}
}

void CommandBufferManager::SubmitGlobalCommandBuffer()
{
#if defined(ENABLE_RENDER_THREAD)
	mSubmitGlobal = std::async(std::launch::async, [this]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
		CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(gpSwapchainManager->miFramebufferIndex);

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
			.pCommandBuffers = &rCommandBuffers.mpGlobalCommandBuffers[rCommandBuffers.miCurrentIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rCommandBuffers.mpGlobalFinishedVkSemaphores[rCommandBuffers.miCurrentIndex],
		};

		CPU_PROFILE_STOP_AND_SMOOTH(kCpuTimerAcquireToGlobal);

		CPU_PROFILE_START(kCpuTimerSubmitGlobal);
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
		CPU_PROFILE_STOP(kCpuTimerSubmitGlobal);

		rCommandBuffers.mpbExecuted[rCommandBuffers.miCurrentIndex] = true;
#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

void CommandBufferManager::SubmitImageCommandBuffer()
{
#if defined(ENABLE_RENDER_THREAD)
	mSubmitImage = std::async(std::launch::async, [this]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

		CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(gpSwapchainManager->miFramebufferIndex);

		std::vector<VkSemaphore> vkSemaphores;
		std::vector<VkPipelineStageFlags> vkPipelineStageFlags;
		vkSemaphores.emplace_back(rCommandBuffers.mpGlobalFinishedVkSemaphores[rCommandBuffers.miCurrentIndex]);
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
			.pCommandBuffers = &rCommandBuffers.mpImageCommandBuffers[rCommandBuffers.miCurrentIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rCommandBuffers.mpImageFinishedVkSemaphores[rCommandBuffers.miCurrentIndex],
		};
		CPU_PROFILE_START(kCpuTimerSubmitImage);
		CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mpVkFences[rCommandBuffers.miCurrentIndex]));
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, rCommandBuffers.mpVkFences[rCommandBuffers.miCurrentIndex]));
		CPU_PROFILE_STOP(kCpuTimerSubmitImage);

	#if defined(ENABLE_SCREENSHOTS)
		if (mbSaveScreenshot)
		{
			mbSaveScreenshot = false;
			SaveScreenshot();
		}
	#endif
	
#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

} // namespace engine
