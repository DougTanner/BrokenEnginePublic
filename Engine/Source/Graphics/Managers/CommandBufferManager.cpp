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
	gpProfileManager->ResetQueryPools(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal, kGpuTimerMain);

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
	uint32_t uiShadowWidth = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width;
	uint32_t uiShadowHeight = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height;
	pPipelines[kPipelineShadow].RecordCompute(iCommandBuffer, vkCommandBuffer, uiShadowWidth, uiShadowHeight / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurH].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiShadowTextureExecutionSize - 1) / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineShadowBlurV].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiShadowWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiShadowHeight + shaders::kiShadowTextureExecutionSize - 1) / shaders::kiShadowTextureExecutionSize);
	gpTextureManager->mRenderTargetTextures.mShadowTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mShadowBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerShadow);

	RecordTerrainPasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	uint32_t uiWindWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	RecordWindSpreadPipeline(vkCommandBuffer, iCommandBuffer, uiWindWidth, pPipelines);

	uint32_t uiSmokeTilesX = (std::max(gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent.width) + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	RecordSmokeSpreadPipeline(vkCommandBuffer, iCommandBuffer, uiSmokeTilesX, pPipelines);

	RecordParticleUpdatePasses(vkCommandBuffer, iCommandBuffer, pPipelines);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerGlobal);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferManager::RecordTerrainPasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	int64_t iIslandCount = static_cast<int64_t>(gpIslands->mIslands.size());

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainElevation].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainElevation);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);
	gpTextureManager->mRenderTargetTextures.mTerrainColorTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainColor].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainColorTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainColor);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);
	gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainNormal].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainNormal);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
	gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture.RecordBeginRenderPass(vkCommandBuffer);
	pPipelines[kPipelineTerrainAmbientOcclusion].RecordDraw(iCommandBuffer, vkCommandBuffer, iIslandCount, 0);
	gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrainAmbientOcclusion);
}

void CommandBufferManager::RecordWindSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiWindWidth, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);

	uint32_t uiWindTilesX = (uiWindWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	uint32_t uiWindDilateGroups = (uiWindTilesX * uiWindTilesX + shaders::kiOccupancyDilateGroupSize - 1) / shaders::kiOccupancyDilateGroupSize;

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
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineWindSpreadComputeB].mVkPipelineLayout, 0, 1, &pPipelines[kPipelineWindSpreadComputeB].mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[1], 0);
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// SpreadA: dispatch from ActiveTileA (reads TextureTwo, writes TextureOne)
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	{
		int64_t iDescriptorSetIndex = pPipelines[kPipelineWindSpreadComputeA].mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineWindSpreadComputeA].mVkPipeline);
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineWindSpreadComputeA].mVkPipelineLayout, 0, 1, &pPipelines[kPipelineWindSpreadComputeA].mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mWindActiveTileVkBuffers[0], 0);
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindSpread);
}

void CommandBufferManager::RecordSmokeSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, uint32_t uiSmokeTilesX, Pipeline* pPipelines)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);

	uint32_t uiTotalTiles = uiSmokeTilesX * uiSmokeTilesX;
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
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineSmokeSpreadComputeB].mVkPipelineLayout, 0, 1, &pPipelines[kPipelineSmokeSpreadComputeB].mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
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

	// Dilate: read SpreadB's occupancy, write active tile list for SpreadA
	pPipelines[kPipelineSmokeOccupancyDilate].RecordCompute(iCommandBuffer, vkCommandBuffer, uiDilateGroups);

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
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pPipelines[kPipelineSmokeSpreadComputeA].mVkPipelineLayout, 0, 1, &pPipelines[kPipelineSmokeSpreadComputeA].mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
		vkCmdDispatchIndirect(vkCommandBuffer, gpBufferManager->mSmokeActiveTileVkBuffer, 0);
	}
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeSpread);
}

void CommandBufferManager::RecordParticleUpdatePasses(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, Pipeline* pPipelines)
{
	BarrierInfo pBarriers[]
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

void CommandBufferManager::RecordLightingSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	RenderTargetTextures& rRenderTargetTextures = gpTextureManager->mRenderTargetTextures;

	VkMemoryBarrier vkComputeBarrier
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};

	uint32_t uiSpreadWidth = rRenderTargetTextures.mpSpreadTextures[0][0].mInfo.extent.width;
	uint32_t uiSpreadHeight = rRenderTargetTextures.mpSpreadTextures[0][0].mInfo.extent.height;

	// Phase 1: Radial spread passes (fragment shader with MRT, chained: deposit → spread[0] → spread[1] → ...)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingSpread);
	{
		int64_t iSpreadPassCount = static_cast<int64_t>(gSpreadPassCount.Get());
		VkMemoryBarrier vkSpreadBarrier
		{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		};

		for (int64_t iPass = 0; iPass < iSpreadPassCount; ++iPass)
		{
			VkClearValue pSpreadClearValues[3] {};
			VkRenderPassBeginInfo vkSpreadRenderPassBeginInfo
			{
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.pNext = nullptr,
				.renderPass = rRenderTargetTextures.mSpreadVkRenderPass,
				.framebuffer = rRenderTargetTextures.mpSpreadVkFramebuffers[iPass],
				.renderArea = {.offset = {0, 0}, .extent = {uiSpreadWidth, uiSpreadHeight}},
				.clearValueCount = 3,
				.pClearValues = pSpreadClearValues,
			};
			vkCmdBeginRenderPass(vkCommandBuffer, &vkSpreadRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			gpPipelineManager->mSpreadPipelines[iPass].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(uiSpreadWidth), static_cast<float>(uiSpreadHeight), static_cast<float>(iPass), 0.0f});
			vkCmdEndRenderPass(vkCommandBuffer);

			// Barrier between spread passes (color attachment write → fragment shader read for next pass)
			if (iPass < iSpreadPassCount - 1)
			{
				vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &vkSpreadBarrier, 0, nullptr, 0, nullptr);
			}
		}

		// Final barrier: last spread output → combine compute shader read
		vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &vkSpreadBarrier, 0, nullptr, 0, nullptr);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingSpread);

	// Phase 3: Combine (tone map accumulate float16 → UNORM)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	}

	{
		Pipeline& rCombinePipeline = gpPipelineManager->mCombinePipeline;
		int64_t iDescriptorSetIndex = rCombinePipeline.mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rCombinePipeline.mVkPipeline);
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rCombinePipeline.mVkPipelineLayout, 0, 1, &rCombinePipeline.mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);

		shaders::PushConstantsLayout combinePushConstants {};
		struct CombineData { uint32_t uiWidth; uint32_t uiHeight; };
		CombineData combineData
		{
			.uiWidth = uiSpreadWidth,
			.uiHeight = uiSpreadHeight,
		};
		std::memcpy(&combinePushConstants, &combineData, std::min(sizeof(combineData), sizeof(combinePushConstants)));

		uint32_t uiCombineGroupsX = (uiSpreadWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		uint32_t uiCombineGroupsY = (uiSpreadHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		vkCmdPushConstants(vkCommandBuffer, rCombinePipeline.mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shaders::PushConstantsLayout), &combinePushConstants);
		vkCmdDispatch(vkCommandBuffer, uiCombineGroupsX, uiCombineGroupsY, 1);
	}

	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	}
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Final barrier: compute → fragment (combine textures are now readable by fragment shaders)
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &vkComputeBarrier, 0, nullptr, 0, nullptr);
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
	gpProfileManager->ResetQueryPools(iCommandBuffer, vkCommandBuffer, kGpuTimerMain, kGpuTimerUiRender);

	if constexpr (kbEnableDebugPrintf)
	{
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineLog].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(iCommandBuffer), 0.0f, 0.0f, 0.0f});
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordEndRenderPass(vkCommandBuffer);
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);
	gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingDeposit);

	// Clear occupancy buffer before deposit (deposit shaders write occupancy)
	VkBufferMemoryBarrier vkOccupancyPreClearBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = gpBufferManager->mLightOccupancyVkBuffers[0],
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &vkOccupancyPreClearBarrier, 0, nullptr);
	vkCmdFillBuffer(vkCommandBuffer, gpBufferManager->mLightOccupancyVkBuffers[0], 0, gpBufferManager->mLightOccupancyBufferSizes[0], 0);

	VkBufferMemoryBarrier vkOccupancyClearToFragmentBarrier
	{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = gpBufferManager->mLightOccupancyVkBuffers[0],
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 1, &vkOccupancyClearToFragmentBarrier, 0, nullptr);

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
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {3.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineAxisAlignedLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {3.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineHexShieldsLighting])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {3.0f, 0.0f, 0.0f, 0.0f});
	}
	pPipelines[kPipelineLongParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {3.0f, 0.0f, 0.0f, 0.0f});
	pPipelines[kPipelineSquareParticlesLighting].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {3.0f, 0.0f, 0.0f, 0.0f});
	vkCmdEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingDeposit);

	// Barrier: deposit MRT color attachment + occupancy writes → compute shader reads (spread)
	VkMemoryBarrier vkDepositToComputeBarrier
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &vkDepositToComputeBarrier, 0, nullptr, 0, nullptr);

	RecordLightingSpreadPipeline(vkCommandBuffer, iCommandBuffer);

	// Smoke emit pass
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmokeAxisAligned])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineSmoke])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);

	// Wind deposit pass A (writes TextureOne)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWindDeposit);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositA])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureOne.RecordEndRenderPass(vkCommandBuffer);

	// Wind deposit pass B (writes TextureTwo)
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositB])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {2.0f, 0.0f, 0.0f, 0.0f});
	}
	gpTextureManager->mRenderTargetTextures.mWindTextureTwo.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWindDeposit);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkFramebuffer, {gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.height}, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.renderPassVkClearColorValue, RenderPassFlags_t {RenderPassFlags::kClear}, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f}, ModelDrawPass::kOpaque);
	}
	gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);

	// Object shadows blur passes (separable compute)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
	uint32_t uiObjectShadowsBlurWidth = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mInfo.extent.width;
	uint32_t uiObjectShadowsBlurHeight = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mInfo.extent.height;
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineObjectShadowsBlurH].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiObjectShadowsBlurWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiObjectShadowsBlurHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineObjectShadowsBlurV].RecordCompute(iCommandBuffer, vkCommandBuffer, (uiObjectShadowsBlurWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (uiObjectShadowsBlurHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
	{
		RenderPassFlags_t renderPassFlags {RenderPassFlags::kDepth};
		renderPassFlags.Set(RenderPassFlags::kClear);
		if (gMultisampling.Get<bool>())
		{
			renderPassFlags.Set(RenderPassFlags::kMultisampling);
		}
		Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, renderPassFlags, VK_SUBPASS_CONTENTS_INLINE);
	}

	bool bDebugTextureMode = false;
	if constexpr (kbEnableDebugInput)
	{
		if (gDebugTexture.Get<bool>())
		{
			pPipelines[kPipelineDebugTexture].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
			bDebugTextureMode = true;
		}
	}

	if (!bDebugTextureMode)
	{
		gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjects);
		for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModel])
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
		for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineHexShields])
		{
			pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
		}
		gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerHexShields);

		gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerTransparentObjects);
		// Transparent model pass (after all opaque geometry and water for correct blending)
		for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModel])
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
		for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights])
		{
			pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		}
		gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerVisibleLights);

		gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);
		for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mPipelineMaps[kDynamicPipelineBillboards])
		{
			pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		}
		gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerBillboards);
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerText);
	pPipelines[kPipelineProfileText].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerText);

	Texture::RecordEndRenderPass(vkCommandBuffer);
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
