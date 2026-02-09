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

	ScopedBootTimer scopedBootTimer(kBootTimerCommandBufferManager);

	// Because one of the drawing commands involves binding the right VkFramebuffer, we'll actually have to record a command buffer for every image in the swap chain
	mPerFramebufferCommandBuffers.reserve(gpSwapchainManager->mFramebuffers.size());
	for (int64_t i = 0; i < static_cast<int64_t>(gpSwapchainManager->mFramebuffers.size()); ++i)
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
	rCommandBuffers.mFlags |= CommandBufferFlags::kRecorded;
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
	gpTextureManager->mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0, {1.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, gpTextureManager->mShadowTexture.mInfo.extent.width, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlur].RecordCompute(iCommandBuffer, vkCommandBuffer, 1, gpTextureManager->mShadowTexture.mInfo.extent.height / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);
	gpTextureManager->mTerrainColorTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainColor].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainColorTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);
	gpTextureManager->mTerrainNormalTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainNormal].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainNormalTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
	gpTextureManager->mTerrainAmbientOcclusionTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainAmbientOcclusion].RecordDraw(iCommandBuffer, vkCommandBuffer, static_cast<int64_t>(gpIslands->mIslands.size()), 0);
	gpTextureManager->mTerrainAmbientOcclusionTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);
	gpTextureManager->mSmokeTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadTwo].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearTwo].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mSmokeTextureTwo.RecordEndRenderPass(vkCommandBuffer);
	gpTextureManager->mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineSmokeSpreadOne].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	pPipelines[kPipelineSmokeClearOne].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

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

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);
	pPipelines[kPipelineLongParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
	}));
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
	pPipelines[kPipelineSquareParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	Buffer::RecordBarriers(vkCommandBuffer, std::to_array<BarrierInfo>(
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
	}));
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
		gpTextureManager->mLogTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineLog].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(iCommandBuffer), 0.0f, 0.0f, 0.0f});
		gpTextureManager->mLogTexture.RecordEndRenderPass(vkCommandBuffer);
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
	gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	// Memory barrier: ensure CPU-written data (mesh data, joint matrices, indirect buffers) is visible to GPU
	VkMemoryBarrier memoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLighting);
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
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesAxisAlignedLightingMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesHexShieldsLightingMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}
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
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingBlur);

	// Lighting combine passes
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);
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
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Smoke emit pass
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);
	gpTextureManager->mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesSmokeAxisAlignedMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesSmokeMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mObjectShadowsTexture.mVkFramebuffer, {gpTextureManager->mObjectShadowsTexture.mInfo.extent.width, gpTextureManager->mObjectShadowsTexture.mInfo.extent.height}, gpTextureManager->mObjectShadowsTexture.mInfo.renderPassVkClearColorValue, false, false, true, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicModelPipelineShadowMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);

	// Object shadows blur pass
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
	gpTextureManager->mObjectShadowsBlurTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineObjectShadowsBlur].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpTextureManager->mObjectShadowsBlurTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, true, gMultisampling.Get<bool>(), true, VK_SUBPASS_CONTENTS_INLINE);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicModelPipelineMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);
	pPipelines[kPipelineTerrain].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);
	pPipelines[kPipelineWater].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {0.0f, 0.0f, 0.0f, 0.0f});
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);

	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesHexShieldsMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);
	pPipelines[kPipelineLongParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesRender);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);
	pPipelines[kPipelineSquareParticlesRender].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesRender);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesVisibleLightsMap)
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);
	for (const auto& [crc, pPipeline] : gpPipelineManager->mDynamicPipelinesBillboardsMap)
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

void CommandBufferManager::SubmitGlobalCommandBufferImpl(int64_t iFramebufferIndex)
{
	CommandBuffers& rCommandBuffers = mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	// Prepend acquire barrier command buffer for QFOT when textures were adopted this frame
	VkCommandBuffer pCommandBuffers[2];
	uint32_t uiCommandBufferCount = 0;
	if (gpTextureManager->mbHasPendingAcquireBarriers)
	{
		pCommandBuffers[uiCommandBufferCount++] = gpTextureManager->mAcquireVkCommandBuffers.at(gpTextureManager->miAcquireFramebufferIndex);
	}
	pCommandBuffers[uiCommandBufferCount++] = rCommandBuffers.mGlobalVkCommandBuffer;

	std::vector<VkSemaphore> vkSemaphores;
	std::vector<VkPipelineStageFlags> vkPipelineStageFlags;

	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = static_cast<uint32_t>(vkSemaphores.size()),
		.pWaitSemaphores = vkSemaphores.data(),
		.pWaitDstStageMask = vkPipelineStageFlags.data(),
		.commandBufferCount = uiCommandBufferCount,
		.pCommandBuffers = pCommandBuffers,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &rCommandBuffers.mGlobalFinishedVkSemaphore,
	};

	gpProfileManager->CpuStop(kCpuTimerAcquireToGlobal, true);

	gpProfileManager->CpuStart(kCpuTimerSubmitGlobal);
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitGlobal, false);

	rCommandBuffers.mFlags |= CommandBufferFlags::kExecuted;
}

void CommandBufferManager::SubmitGlobalCommandBuffer(int64_t iFramebufferIndex)
{
	if constexpr (kbEnableRenderThread)
	{
		mSubmitGlobal = std::async(std::launch::async, [this, iFramebufferIndex]()
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
			SubmitGlobalCommandBufferImpl(iFramebufferIndex);
		});
	}
	else
	{
		SubmitGlobalCommandBufferImpl(iFramebufferIndex);
	}
}

void CommandBufferManager::SubmitMainCommandBufferImpl(int64_t iFramebufferIndex, bool bSignalFence)
{
	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);

	std::vector<VkSemaphore> vkSemaphores;
	std::vector<VkPipelineStageFlags> vkPipelineStageFlags;
	vkSemaphores.emplace_back(rCommandBuffers.mGlobalFinishedVkSemaphore);
	vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	vkSemaphores.emplace_back(gpSwapchainManager->mImageAvailableVkSemaphore);
	vkPipelineStageFlags.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	VkSubmitInfo vkSubmitInfo
	{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = static_cast<uint32_t>(vkSemaphores.size()),
		.pWaitSemaphores = vkSemaphores.data(),
		.pWaitDstStageMask = vkPipelineStageFlags.data(),
		.commandBufferCount = 1,
		.pCommandBuffers = &rCommandBuffers.mMainVkCommandBuffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &rCommandBuffers.mMainFinishedVkSemaphore,
	};
	gpProfileManager->CpuStart(kCpuTimerSubmitImage);
	CHECK_VK(vkResetFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence));
	CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, bSignalFence ? rCommandBuffers.mVkFence : VK_NULL_HANDLE));
	gpProfileManager->CpuStop(kCpuTimerSubmitImage, false);

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
		mSubmitMain = std::async(std::launch::async, [this, iFramebufferIndex, bSignalFence]()
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
			mSubmitGlobal.get();
			SubmitMainCommandBufferImpl(iFramebufferIndex, bSignalFence);
		});
	}
	else
	{
		SubmitMainCommandBufferImpl(iFramebufferIndex, bSignalFence);
	}
}

void CommandBufferManager::SubmitUiCommandBuffer(int64_t iFramebufferIndex)
{
	if (mSubmitMain.valid())
	{
		mSubmitMain.wait();
	}

	gpImGuiManager->Submit(iFramebufferIndex);
}

} // namespace engine
