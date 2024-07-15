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

void CommandBufferManager::RecordAllCommandBuffers()
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

void CommandBufferManager::RecordCommandBuffer(int64_t iFramebuffer)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebuffer);
	Pipeline* pPipelines = gpPipelineManager->mpPipelines;

	if (rCommandBuffers.mpbRecorded[rCommandBuffers.miCurrentIndex])
	{
		return;
	}
	rCommandBuffers.mpbRecorded[rCommandBuffers.miCurrentIndex] = true;
	rCommandBuffers.mpbExecuted[rCommandBuffers.miCurrentIndex] = false;

	int64_t iCommandBuffer = gpCommandBufferManager->CommandBufferIndex(iFramebuffer);
	LOG("Record command buffer: {} {} -> {}", iFramebuffer, rCommandBuffers.miCurrentIndex, iCommandBuffer);

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
		pPipelines[kPipelineLongParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
		pPipelines[kPipelineSquareParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
		gpTextureManager->mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miCount, 0, {1.0f, 0.0f, 0.0f, 0.0f});
		gpTextureManager->mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
		pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, gpTextureManager->mShadowTexture.mInfo.extent.width, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
		gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeWrite, kShaderReadOnly);
		gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeWrite);
		pPipelines[kPipelineShadowBlur].RecordCompute(iCommandBuffer, vkCommandBuffer, 1, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
		gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeWrite);
		gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeWrite, kShaderReadOnly);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
		gpTextureManager->mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miCount, 0);
		gpTextureManager->mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);
		gpTextureManager->mTerrainColorTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineTerrainColor].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miCount, 0);
		gpTextureManager->mTerrainColorTexture.RecordEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);
		gpTextureManager->mTerrainNormalTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineTerrainNormal].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miCount, 0);
		gpTextureManager->mTerrainNormalTexture.RecordEndRenderPass(vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
		gpTextureManager->mTerrainAmbientOcclusionTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineTerrainAmbientOcclusion].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miCount, 0);
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

		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kComputeRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesUpdate].mIndirectVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesRender].mIndirectVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesLighting].mIndirectVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kComputeRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesUpdate].mIndirectVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesRender].mIndirectVkBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesLighting].mIndirectVkBuffer);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);
		pPipelines[kPipelineLongParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderUniformRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
		pPipelines[kPipelineSquareParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
		Buffer::RecordBarrier(vkCommandBuffer, BufferBarrier::kComputeWrite, BufferBarrier::kShaderUniformRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);

		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

		CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
	}

	{
		// Main command buffer
		VkCommandBuffer vkCommandBuffer = rCommandBuffers.mpMainCommandBuffers[rCommandBuffers.miCurrentIndex];
		CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
		PROFILE_MANAGER_RESET_MAIN_QUERY_POOLS(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
	
		gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);
		for (int64_t i = 0; i < 3; ++i)
		{
			gpTextureManager->mpLightingTextures[i].RecordBeginRenderPass(vkCommandBuffer);
			pPipelines[kPipelineAreaLights].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, static_cast<float>(i), 0.0f});
			pPipelines[kPipelinePointLights].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, static_cast<float>(i), 0.0f});
			pPipelines[kPipelineHexShieldsLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 1.0f, static_cast<float>(i), 0.0f});
			pPipelines[kPipelineLongParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, static_cast<float>(i), 0.0f});
			pPipelines[kPipelineSquareParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, static_cast<float>(i), 0.0f});
			gpTextureManager->mpLightingTextures[i].RecordEndRenderPass(vkCommandBuffer);
		}
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
		pPipelines[kPipelineSmokePuffs].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
		pPipelines[kPipelineSmokeTrails].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
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
		CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
	}

	{
		// Image command buffer
		VkCommandBuffer vkCommandBuffer = rCommandBuffers.mpImageCommandBuffers[rCommandBuffers.miCurrentIndex];
		CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
		PROFILE_MANAGER_RESET_IMAGE_QUERY_POOLS(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

		Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, true, gMultisampling.Get<bool>(), true);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);
		game::gpGltfPipelines->RecordGltfPipelines(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);
		pPipelines[kPipelineTerrain].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);
		pPipelines[kPipelineWater].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {0.0f, 0.0f, 0.0f, 0.0f});
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);
		pPipelines[kPipelineLongParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);
		pPipelines[kPipelineSquareParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);

		pPipelines[kPipelineHexShields].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);
		pPipelines[kPipelineVisibleLights].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);

		GPU_PROFILE_START(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);
		pPipelines[kPipelineBillboards].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		GPU_PROFILE_STOP(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);

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

void CommandBufferManager::SubmitMainCommandBuffer()
{
#if defined(ENABLE_RENDER_THREAD)
	mSubmitMain = std::async(std::launch::async, [this]()
	{
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif

		CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(gpSwapchainManager->miFramebufferIndex);

		std::vector<VkSemaphore> vkSemaphores;
		std::vector<VkPipelineStageFlags> vkPipelineStageFlags;
		vkSemaphores.emplace_back(rCommandBuffers.mpGlobalFinishedVkSemaphores[rCommandBuffers.miCurrentIndex]);
		vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

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
			.pCommandBuffers = &rCommandBuffers.mpMainCommandBuffers[rCommandBuffers.miCurrentIndex],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &rCommandBuffers.mpMainFinishedVkSemaphores[rCommandBuffers.miCurrentIndex],
		};
		CPU_PROFILE_START(kCpuTimerSubmitMain);
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
		CPU_PROFILE_STOP(kCpuTimerSubmitMain);
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
		vkSemaphores.emplace_back(rCommandBuffers.mpMainFinishedVkSemaphores[rCommandBuffers.miCurrentIndex]);
		vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
		vkSemaphores.emplace_back(gpSwapchainManager->mImageAvailableVkSemaphore);
		vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	#if defined(ENABLE_RENDER_THREAD)
		mSubmitMain.get();
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
		if (mbSaveScreenshots && mScreenshotTimer.GetDeltaNs() > 2s)
		{
			mScreenshotTimer.Reset();
			SaveScreenshot();
			game::gpGame->ResetRealTime();
		}
	#endif
	
#if defined(ENABLE_RENDER_THREAD)
	});
#endif
}

} // namespace engine
