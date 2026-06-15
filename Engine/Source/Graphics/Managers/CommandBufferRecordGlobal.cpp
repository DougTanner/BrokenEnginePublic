#if defined(BT_CLIENT)

#include "CommandBufferRecordGlobal.h"

#include "CommandBufferManager.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum TextureLayout;

void CommandBufferRecordGlobal::Record(int64_t iFramebuffer)
{
	gpPipelineManager->VerifyAllDescriptorGenerations();

	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebuffer);
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
	gpProfileManager->ResetQueryPools(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miTemplateCount * kiMaxPlacementsPerTemplate, 0, {1.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	uint32_t uiShadowWidth = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width;
	uint32_t uiShadowHeight = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height;
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurH].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurV].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	// Temporal accumulation: blend the reprojected previous-frame shadow into mShadowBlurTexture in place, then
	// copy the result into mShadowHistoryTexture for next frame. De-flickers the texel-ramp resample.
	// Runs unconditionally every frame even when gShadowTemporalBlend == 1.0 (disabled): the mix() is then a no-op
	// but the dispatch + full-texture copy still execute. Not gated by design — the recorded copy can't be indirect-gated
	// (region fixed at record time; a fast zoom-out can grow the window to the full texture), and skipping the pass
	// would need a destroy-tier CB re-record on the enable/disable edge (blend currently flows purely through the uniform).
	// The fixed cost is small and accepted. The copy is also full-texture-extent so a post-recreate reset frame
	// (gbShadowTemporalReset -> PopulateShadowParameters) reseeds the entire history in one frame, not just the window.
	// Same-layout self-transition: barrier so ShadowBlurV's writes finish before ShadowTemporal reads them in place
	// (the blur chain otherwise relies on the per-pass input layout change to carry this dependency; here the layout is unchanged).
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadOnly);
	pPipelines[kPipelineShadowTemporal].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kTransferSource);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kTransferDestination);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.RecordCopyImageFrom(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mShadowBlurTexture);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kTransferSource, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.TransitionImageLayout(vkCommandBuffer, kTransferDestination, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

	RecordTerrainPasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	uint32_t uiWindWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	uint32_t uiWindHeight = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.height;
	uint32_t uiWindTilesX = (uiWindWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiWindTilesY = (uiWindHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;

	uint32_t uiSmokeMaxWidth = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	uint32_t uiSmokeMaxHeight = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.height, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.height);
	uint32_t uiSmokeTilesX = (uiSmokeMaxWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiSmokeTilesY = (uiSmokeMaxHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSpread);
	RecordWindSpreadPipeline(vkCommandBuffer, iCommandBuffer, uiWindTilesX, uiWindTilesY, pPipelines);
	RecordSmokeSpreadPipeline(vkCommandBuffer, iCommandBuffer, uiSmokeTilesX, uiSmokeTilesY, pPipelines);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSpread);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerParticles);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);
	pPipelines[kPipelineLongParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
	pPipelines[kPipelineSquareParticlesSpawn].RecordCompute(iCommandBuffer, vkCommandBuffer, 1);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerParticlesSpawn);

	RecordParticleUpdatePasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerParticles);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferRecordGlobal::RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	// Total SSBO slot count = N_templates × kiMaxPlacementsPerTemplate, fixed at boot. Inactive
	// slots are zero-width quads that QuadsAxisAlignedVisibleArea.vert culls via degenerate
	// triangles (no real GPU cost).
	int64_t iIslandCount = gpIslands->miTemplateCount * kiMaxPlacementsPerTemplate;

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainGen);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainGen);
}

void CommandBufferRecordGlobal::RecordWindSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiWindTilesX, uint32_t uiWindTilesY, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);

	uint32_t uiWindDilateGroups = (uiWindTilesX * uiWindTilesY + shaders::kiOccupancyDilateGroupSize - 1) / shaders::kiOccupancyDilateGroupSize;

	// Reset both active tile buffers: {0, 1, 1}
	uint32_t pWindResetCmd[3] {0, 1, 1};
	vkCmdUpdateBuffer(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[0], 0, sizeof(pWindResetCmd), pWindResetCmd);
	vkCmdUpdateBuffer(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[1], 0, sizeof(pWindResetCmd), pWindResetCmd);

	// Barrier: transfer -> compute (both active tile buffers)
	VkBufferMemoryBarrier pWindActiveTileResetBarriers[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindActiveTileVkBuffers[0],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindActiveTileVkBuffers[1],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pWindActiveTileResetBarriers)), pWindActiveTileResetBarriers, 0, nullptr);

	// Both dilates (read different occupancy buffers, write different active tile buffers)
	pPipelines[kPipelineWindOccupancyDilateB].RecordCompute(iCommandBuffer, vkCommandBuffer, uiWindDilateGroups);
	pPipelines[kPipelineWindOccupancyDilateA].RecordCompute(iCommandBuffer, vkCommandBuffer, uiWindDilateGroups);

	// Barrier: compute read/write -> transfer write (occupancy), compute read/write -> indirect+compute (active tiles)
	VkBufferMemoryBarrier pWindDilateBarriers[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindActiveTileVkBuffers[0],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindActiveTileVkBuffers[1],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindOccupancyVkBuffers[0],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindOccupancyVkBuffers[1],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pWindDilateBarriers)), pWindDilateBarriers, 0, nullptr);

	// Clear both occupancy buffers
	vkCmdFillBuffer(vkCommandBuffer, gpBufferManager->mWindOccupancyVkBuffers[0], 0, gpBufferManager->mWindOccupancyBufferSize, 0);
	vkCmdFillBuffer(vkCommandBuffer, gpBufferManager->mWindOccupancyVkBuffers[1], 0, gpBufferManager->mWindOccupancyBufferSize, 0);

	// Barrier: transfer -> compute (both occupancy buffers)
	VkBufferMemoryBarrier pWindOccupancyClearBarriers[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindOccupancyVkBuffers[0],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mWindOccupancyVkBuffers[1],
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pWindOccupancyClearBarriers)), pWindOccupancyClearBarriers, 0, nullptr);

	// SpreadB: dispatch from ActiveTileB (reads TextureOne, writes TextureTwo)
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	{
		int64_t iDescriptorSetIndex = pPipelines[kPipelineWindSpreadComputeB].mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineWindSpreadComputeB].mVkPipeline);
		BindComputeDescriptorSets(vkCommandBuffer, pPipelines[kPipelineWindSpreadComputeB].mVkPipelineLayout, pPipelines[kPipelineWindSpreadComputeB].mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, pPipelines[kPipelineWindSpreadComputeB].mVkDescriptorSets);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[1], 0);
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// SpreadA: dispatch from ActiveTileA (reads TextureTwo, writes TextureOne)
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	{
		int64_t iDescriptorSetIndex = pPipelines[kPipelineWindSpreadComputeA].mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineWindSpreadComputeA].mVkPipeline);
		BindComputeDescriptorSets(vkCommandBuffer, pPipelines[kPipelineWindSpreadComputeA].mVkPipelineLayout, pPipelines[kPipelineWindSpreadComputeA].mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, pPipelines[kPipelineWindSpreadComputeA].mVkDescriptorSets);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[0], 0);
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);
}

void CommandBufferRecordGlobal::RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, uint32_t uiSmokeTilesY, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

	uint32_t uiTotalTiles = uiSmokeTilesX * uiSmokeTilesY;
	uint32_t uiDilateGroups = (uiTotalTiles + shaders::kiOccupancyDilateGroupSize - 1) / shaders::kiOccupancyDilateGroupSize;

	// === Dilate + Compact for SpreadB ===
	// Reset active tile buffer indirect command: {0, 1, 1}
	uint32_t pResetCmd[3] {0, 1, 1};
	vkCmdUpdateBuffer(vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0, sizeof(pResetCmd), pResetCmd);

	// Barrier: transfer write → compute read for active tile buffer
	VkBufferMemoryBarrier vkActiveTileResetBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = gpBufferManager->mSmokeActiveTileVkBuffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vkActiveTileResetBarrier, 0, nullptr);

	// Dilate: read occupancy (from prev frame deposit + spread), write active tile list
	pPipelines[kPipelineSmokeOccupancyDilate].RecordCompute(iCommandBuffer, vkCommandBuffer, uiDilateGroups);

	// Barrier: dilate compute → indirect read + compute read (active tile), compute read → transfer write (occupancy)
	VkBufferMemoryBarrier pDilateBarriersB[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeActiveTileVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeOccupancyVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pDilateBarriersB)), pDilateBarriersB, 0, nullptr);

	// Clear occupancy before SpreadB writes fresh marks
	vkCmdFillBuffer(vkCommandBuffer, gpBufferManager->mSmokeOccupancyVkBuffer, 0, gpBufferManager->mSmokeOccupancyBufferSize, 0);
	VkBufferMemoryBarrier vkOccupancyClearBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = gpBufferManager->mSmokeOccupancyVkBuffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vkOccupancyClearBarrier, 0, nullptr);

	// Clear TextureTwo before indirect spread writes active tiles only
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kTransferDestination);
	VkClearColorValue vkSmokeClearColor {{0.0f, 0.0f, 0.0f, 0.0f}};
	VkImageSubresourceRange vkSmokeSubresource {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
	vkCmdClearColorImage(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkSmokeClearColor, 1, &vkSmokeSubresource);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.TransitionImageLayout(vkCommandBuffer, kTransferDestination, kComputeReadWrite);

	// SpreadB: indirect dispatch from active tile buffer
	{
		int64_t iDescriptorSetIndex = pPipelines[kPipelineSmokeSpreadComputeB].mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineSmokeSpreadComputeB].mVkPipeline);
		BindComputeDescriptorSets(vkCommandBuffer, pPipelines[kPipelineSmokeSpreadComputeB].mVkPipelineLayout, pPipelines[kPipelineSmokeSpreadComputeB].mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, pPipelines[kPipelineSmokeSpreadComputeB].mVkDescriptorSets);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0);
	}
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// === Dilate + Compact for SpreadA ===
	// Barrier: SpreadB compute writes (occupancy) → dilate reads; SpreadB indirect+compute reads (active tile) → transfer write
	VkBufferMemoryBarrier pSpreadBBarriers[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeOccupancyVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeActiveTileVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pSpreadBBarriers)), pSpreadBBarriers, 0, nullptr);

	// Reset active tile buffer indirect command: {0, 1, 1}
	vkCmdUpdateBuffer(vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0, sizeof(pResetCmd), pResetCmd);
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vkActiveTileResetBarrier, 0, nullptr);

	// Dilate: read SpreadB's occupancy, write active tile list for SpreadA. SpreadA does
	// a scale-aware sample (current-area UV -> world -> previous-area UV), so the active
	// list must be built with the matching world-coord remap or zoom drops smoke whose
	// remapped sample-tile lies more than ~2 tiles from the output tile.
	pPipelines[kPipelineSmokeOccupancyDilateRemap].RecordCompute(iCommandBuffer, vkCommandBuffer, uiDilateGroups);

	// Barrier: dilate compute → indirect read + compute read (active tile), compute read → transfer write (occupancy)
	VkBufferMemoryBarrier pDilateBarriersA[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeActiveTileVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeOccupancyVkBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pDilateBarriersA)), pDilateBarriersA, 0, nullptr);

	// Clear occupancy before SpreadA writes fresh marks
	vkCmdFillBuffer(vkCommandBuffer, gpBufferManager->mSmokeOccupancyVkBuffer, 0, gpBufferManager->mSmokeOccupancyBufferSize, 0);
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vkOccupancyClearBarrier, 0, nullptr);

	// Clear TextureOne before indirect spread writes active tiles only
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kTransferDestination);
	vkCmdClearColorImage(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkSmokeClearColor, 1, &vkSmokeSubresource);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kTransferDestination, kComputeReadWrite);

	// SpreadA: indirect dispatch from active tile buffer
	{
		int64_t iDescriptorSetIndex = pPipelines[kPipelineSmokeSpreadComputeA].mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineSmokeSpreadComputeA].mVkPipeline);
		BindComputeDescriptorSets(vkCommandBuffer, pPipelines[kPipelineSmokeSpreadComputeA].mVkPipelineLayout, pPipelines[kPipelineSmokeSpreadComputeA].mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, pPipelines[kPipelineSmokeSpreadComputeA].mVkDescriptorSets);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0);
	}
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);
}

void CommandBufferRecordGlobal::RecordParticleUpdatePasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	BarrierInfo pBarriers[]
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineLongParticlesRender].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kComputeRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesUpdate].mIndirectVkBuffer},
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kShaderIndirectRead, pPipelines[kPipelineSquareParticlesRender].mIndirectVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pBarriers, std::size(pBarriers));

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);
	pPipelines[kPipelineLongParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	BarrierInfo pLongParticleBarrier[]
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mLongParticlesStorageBuffer.mDeviceLocalVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pLongParticleBarrier, std::size(pLongParticleBarrier));
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLongParticlesUpdate);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
	pPipelines[kPipelineSquareParticlesUpdate].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	BarrierInfo pSquareParticleBarrier[]
	{
		{BufferBarrier::kComputeReadWrite, BufferBarrier::kStorageBufferRead, gpBufferManager->mSquareParticlesStorageBuffer.mDeviceLocalVkBuffer},
	};
	Buffer::RecordBarriers(vkCommandBuffer, pSquareParticleBarrier, std::size(pSquareParticleBarrier));
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSquareParticlesUpdate);
}

} // namespace engine

#endif // defined(BT_CLIENT)
