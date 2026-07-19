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

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobalUniformCopy);
	gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobalUniformCopy);

	RecordShadowPasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	RecordTerrainPasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	uint32_t uiWindWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	uint32_t uiWindHeight = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.height;
	uint32_t uiWindTilesX = TileCount(uiWindWidth);
	uint32_t uiWindTilesY = TileCount(uiWindHeight);

	uint32_t uiSmokeMaxWidth = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width);
	uint32_t uiSmokeMaxHeight = std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.height, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.height);
	uint32_t uiSmokeTilesX = TileCount(uiSmokeMaxWidth);
	uint32_t uiSmokeTilesY = TileCount(uiSmokeMaxHeight);

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

void CommandBufferRecordGlobal::RecordShadowPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineShadowElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, gpIslands->miTemplateCount * kiMaxPlacementsPerTemplate, 0, {1.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	uint32_t uiShadowWidth = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width;
	uint32_t uiShadowHeight = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height;
	uint32_t uiShadowTilesX = TileCount(uiShadowWidth);
	uint32_t uiShadowTilesY = TileCount(uiShadowHeight);
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, uiShadowTilesX, uiShadowTilesY);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurH].RecordCompute(iCommandBuffer, vkCommandBuffer, uiShadowTilesX, uiShadowTilesY);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurV].RecordCompute(iCommandBuffer, vkCommandBuffer, uiShadowTilesX, uiShadowTilesY);
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
	pPipelines[kPipelineShadowTemporal].RecordCompute(iCommandBuffer, vkCommandBuffer, uiShadowTilesX, uiShadowTilesY);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kTransferSource);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kTransferDestination);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.RecordCopyImageFrom(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mShadowBlurTexture);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kTransferSource, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture.TransitionImageLayout(vkCommandBuffer, kTransferDestination, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);
}

void CommandBufferRecordGlobal::RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	// Total SSBO slot count = N_templates × kiMaxPlacementsPerTemplate, fixed at boot. Inactive
	// slots are zero-width quads that QuadsAxisAlignedVisibleArea.vert culls via degenerate
	// triangles (no real GPU cost).
	int64_t iIslandCount = gpIslands->miTemplateCount * kiMaxPlacementsPerTemplate;

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
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
	pPipelines[kPipelineWindSpreadComputeB].RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[1], 0);
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// SpreadA: dispatch from ActiveTileA (reads TextureTwo, writes TextureOne)
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineWindSpreadComputeA].RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[0], 0);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);
}

void CommandBufferRecordGlobal::RecordSmokeSpreadHalf(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiDilateGroups, Pipeline& rDilatePipeline, VkBuffer vkOutputOccupancyBuffer, Texture& rSmokeTexture, Pipeline& rSpreadPipeline)
{
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

	// Dilate: read input and output occupancy, write active tile list
	rDilatePipeline.RecordCompute(iCommandBuffer, vkCommandBuffer, uiDilateGroups);

	// Barrier: dilate compute → indirect read + compute read (active tile), output occupancy read → transfer write
	VkBufferMemoryBarrier pDilateBarriers[]
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
			.buffer = vkOutputOccupancyBuffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
		},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, static_cast<uint32_t>(std::size(pDilateBarriers)), pDilateBarriers, 0, nullptr);

	// Clear output occupancy after the dilate has consumed its stale-tile union term, then let spread re-mark it.
	vkCmdFillBuffer(vkCommandBuffer, vkOutputOccupancyBuffer, 0, gpBufferManager->mSmokeOccupancyBufferSize, 0);
	VkBufferMemoryBarrier vkOccupancyClearBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = vkOutputOccupancyBuffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vkOccupancyClearBarrier, 0, nullptr);

	// Indirect dispatch from active tile buffer
	rSmokeTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	rSpreadPipeline.RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0);
	rSmokeTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
}

void CommandBufferRecordGlobal::RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, uint32_t uiSmokeTilesY, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

	uint32_t uiTotalTiles = uiSmokeTilesX * uiSmokeTilesY;
	uint32_t uiDilateGroups = (uiTotalTiles + shaders::kiOccupancyDilateGroupSize - 1) / shaders::kiOccupancyDilateGroupSize;

	// SpreadB: dilate the previous frame's occupancy, then indirect-spread into TextureTwo.
	RecordSmokeSpreadHalf(vkCommandBuffer, iCommandBuffer, uiDilateGroups, pPipelines[kPipelineSmokeOccupancyDilate], gpBufferManager->mSmokeOccupancyVkBuffers[1], gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo, pPipelines[kPipelineSmokeSpreadComputeB]);

	// Barrier between halves: SpreadB compute writes (occupancy) → SpreadA dilate reads; SpreadB indirect+compute reads (active tile) → transfer write
	VkBufferMemoryBarrier pSpreadBBarriers[]
	{
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = gpBufferManager->mSmokeOccupancyVkBuffers[1],
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

	// SpreadA: dilate with the scale-aware remap (current-area UV -> world -> previous-area UV, so zoomed smoke
	// whose remapped sample-tile lies more than ~2 tiles from the output tile stays in the active list), then
	// indirect-spread into TextureOne.
	RecordSmokeSpreadHalf(vkCommandBuffer, iCommandBuffer, uiDilateGroups, pPipelines[kPipelineSmokeOccupancyDilateRemap], gpBufferManager->mSmokeOccupancyVkBuffers[0], gpTextureManager->mRenderTargetTextures.mSmokeTextureOne, pPipelines[kPipelineSmokeSpreadComputeA]);

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
