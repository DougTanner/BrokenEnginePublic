#if defined(BT_CLIENT)

#include "CommandBufferRecordMain.h"

#include "CommandBufferManager.h"
#include "Frame/IslandTerrain.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"

namespace engine
{

using enum TextureLayout;

void CommandBufferRecordMain::Record(int64_t iFramebuffer)
{
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

	VkCommandBuffer vkCommandBuffer = rCommandBuffers.mMainVkCommandBuffer;
	CHECK_VK(vkBeginCommandBuffer(vkCommandBuffer, &vkCommandBufferBeginInfo));
	gpProfileManager->ResetQueryPools(iCommandBuffer, vkCommandBuffer, kGpuTimerMain, kGpuTimerUiRender);

	if constexpr (kbDebugPrintf)
	{
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordBeginRenderPass(vkCommandBuffer);
		pPipelines[kPipelineLog].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(iCommandBuffer), 0.0f, 0.0f, 0.0f});
		gpTextureManager->mRenderTargetTextures.mLogTexture.RecordEndRenderPass(vkCommandBuffer);
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerMainUniformCopy);
	gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).RecordCopy(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerMainUniformCopy);

	RecordLightingDeposit(vkCommandBuffer, iCommandBuffer);
	RecordLightingSpreadPipeline(vkCommandBuffer, iCommandBuffer);
	RecordSmokeEmit(vkCommandBuffer, iCommandBuffer);
	RecordWindDeposits(vkCommandBuffer, iCommandBuffer);
	RecordObjectShadows(vkCommandBuffer, iCommandBuffer);
	RecordObjectShadowsBlur(vkCommandBuffer, iCommandBuffer);
	RecordWaterDisplacement(vkCommandBuffer, iCommandBuffer);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerMain);

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);
	RecordImageRenderPass(vkCommandBuffer, iCommandBuffer, iFramebuffer);
	RecordHighDynamicRangeResolve(vkCommandBuffer, iCommandBuffer, iFramebuffer);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
}

void CommandBufferRecordMain::RecordLightingDeposit(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingDeposit);

	RenderTargetTextures& rRenderTargetTextures = gpTextureManager->mRenderTargetTextures;
	VkClearValue pClearValues[3] {};
	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = rRenderTargetTextures.mLightingVkRenderPass,
		.framebuffer = rRenderTargetTextures.mLightingVkFramebuffer,
		.renderArea = { .offset = {0, 0}, .extent = {rRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width, rRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height}},
		.clearValueCount = static_cast<uint32_t>(std::size(pClearValues)),
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
	vkCmdEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingDeposit);

	// Barrier: deposit MRT color attachment writes → spread fragment shader reads
	VkMemoryBarrier vkDepositToFragmentBarrier
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &vkDepositToFragmentBarrier, 0, nullptr, 0, nullptr);
}

void CommandBufferRecordMain::RecordLightingSpreadPipeline(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	RenderTargetTextures& rRenderTargetTextures = gpTextureManager->mRenderTargetTextures;

	VkMemoryBarrier vkComputeBarrier
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};

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
			uint32_t uiPassWidth = rRenderTargetTextures.mpSpreadTextures[iPass][0].mInfo.extent.width;
			uint32_t uiPassHeight = rRenderTargetTextures.mpSpreadTextures[iPass][0].mInfo.extent.height;
			VkClearValue pSpreadClearValues[6] {};
			VkRenderPassBeginInfo vkSpreadRenderPassBeginInfo
			{
				.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
				.pNext = nullptr,
				.renderPass = rRenderTargetTextures.mSpreadVkRenderPass,
				.framebuffer = rRenderTargetTextures.mpSpreadVkFramebuffers[iPass],
				.renderArea = { .offset = {0, 0}, .extent = {uiPassWidth, uiPassHeight}},
				.clearValueCount = static_cast<uint32_t>(std::size(pSpreadClearValues)),
				.pClearValues = pSpreadClearValues,
			};
			// The refresh predicate owns this indirect instance count. Begin/end and the clear stay unconditional:
			// on a cadence skip the LOAD_OP_CLEAR zeroes all 6 attachments, which is what a gather over an
			// all-zero deposit would have written. LightingSpread.frag reads only the pass index (.z).
			vkCmdBeginRenderPass(vkCommandBuffer, &vkSpreadRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			gpPipelineManager->mSpreadPipelines[iPass].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, static_cast<float>(iPass), 0.0f});
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

	// Phase 2: Combine (tone map accumulate float16 → UNORM)
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);

	gpPipelineManager->mCombinePipeline.RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Phase 3: Temporal accumulation reprojects + EMA-blends the previous history into the 4 combine outputs in place,
	// then the history-copy compute pipeline publishes the blended result into distinct history images.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingTemporal);
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadOnly);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadOnly);

	gpPipelineManager->mLightingTemporalPipeline.RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);

	// Same-layout transitions make temporal writes visible to the history-copy shader, whose independent storage outputs
	// preserve temporal's no-sampled/output-alias contract.
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kComputeReadWrite);
	gpPipelineManager->mLightingHistoryCopyPipeline.RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);

	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingTemporal);

	// Final barrier: compute → fragment (combine textures are now readable by fragment shaders)
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &vkComputeBarrier, 0, nullptr, 0, nullptr);
}

void CommandBufferRecordMain::RecordSmokeEmit(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerSmokeEmit);

	// Force-clear TextureTwo on the Smoke enabled/recreate edge. Global spread runs before Main, so any stale
	// occupancy describing this cleared texture is consumed and drained by the next frame's spread.
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.RecordBeginRenderPass(vkCommandBuffer);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearB].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.RecordEndRenderPass(vkCommandBuffer);

	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kColorAttachment);
	gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.RecordBeginRenderPass(vkCommandBuffer);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearA].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
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
}

void CommandBufferRecordMain::RecordWindDeposits(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	// 2026-07-22 Profile, RX 9070 XT driver 32.0.31007.5012, 30+ stable one-second samples.
	// Median rolling avg (p95 current), parked/panning: 1080p 1/1 (1/1) us;
	// 4K 1/1 (1/1) us; rolling max 2-283 us; worst 1 < 167 us.
	// Fixed disabled-state recording accepted: that cost does not justify edge-triggered command-buffer re-recording.
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
}

void CommandBufferRecordMain::RecordObjectShadows(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	// 2026-07-22 Profile, RX 9070 XT driver 32.0.31007.5012, 30+ stable one-second samples.
	// Median rolling avg (p95 current), parked/panning: 1080p 33/34 (35/35) us;
	// 4K 101/101 (104/103) us; rolling component-sum max bounds 309-697 us; worst medians 3 render + 98 blur = 101 < 167 us.
	// Fixed disabled-state recording accepted: continuous day-cycle state has no explicit feature toggle, and measured cost does not justify derived-state edge re-recording or threshold hysteresis.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkRenderPass, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkFramebuffer, {gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.height}, gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.renderPassVkClearColorValue, RenderPassFlags_t {RenderPassFlags::kClear}, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto& [rCrc, pPipeline] : gpPipelineManager->mDynamicPipelines.mModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		pPipeline->RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 2.0f, 0.0f, 0.0f}, ModelDrawPass::kOpaque);
	}
	gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadows);
}

void CommandBufferRecordMain::RecordObjectShadowsBlur(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
	uint32_t uiObjectShadowsBlurWidth = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mInfo.extent.width;
	uint32_t uiObjectShadowsBlurHeight = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mInfo.extent.height;
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	gpPipelineManager->mpPipelines[kPipelineObjectShadowsBlurH].RecordCompute(iCommandBuffer, vkCommandBuffer, TileCount(uiObjectShadowsBlurWidth), TileCount(uiObjectShadowsBlurHeight));
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadOnly);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	gpPipelineManager->mpPipelines[kPipelineObjectShadowsBlurV].RecordCompute(iCommandBuffer, vkCommandBuffer, TileCount(uiObjectShadowsBlurWidth), TileCount(uiObjectShadowsBlurHeight));
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerObjectShadowsBlur);
}

void CommandBufferRecordMain::RecordWaterDisplacement(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer)
{
	// Pre-compute Gerstner wave displacement + Jacobian normal into two RGBA16F textures so the
	// Water.vert pass below can texelFetch a single value per vertex
	// instead of summing iWaterLowCount + iWaterMediumCount waves. Dispatch dims are written per frame
	// by MainUniforms (WriteIndirectComputeBuffer) to cover only the active LOD sub-region; the shader still
	// bounds-checks each thread against iWaterActiveQuad* as a defensive guard. Elevation texture was rendered
	// earlier in the Global command buffer (CommandBufferRecordGlobal.cpp ~line 96), which submits before Main
	// per the acquire-Global-Main-ImGui semaphore chain — no extra elevation barrier required.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterDisplacement);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	gpPipelineManager->mpPipelines[kPipelineWaterDisplacement].RecordComputeIndirect(iCommandBuffer, vkCommandBuffer);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterDisplacement);
}

void CommandBufferRecordMain::RecordImageRenderPass(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, int64_t iFramebuffer)
{
	Pipeline* pPipelines = gpPipelineManager->mpPipelines;
	{
		RenderPassFlags_t renderPassFlags {RenderPassFlags::kDepth};
		renderPassFlags.Set(RenderPassFlags::kClear);
		if (gMultisampling.Get<bool>())
		{
			renderPassFlags.Set(RenderPassFlags::kMultisampling);
		}
		Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mHdrVkRenderPass, gpSwapchainManager->mHdrVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, renderPassFlags, VK_SUBPASS_CONTENTS_INLINE);
	}

	// UI depth pre-pass: depth-only quads at ImGui window positions (instanceCount=0 when disabled)
	{
		gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerUiDepth);
		Pipeline& rUiPrepass = pPipelines[kPipelineUiDepthPrepass];
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rUiPrepass.mVkPipeline);
		VkDescriptorSet pUiPrepassSets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iFramebuffer], rUiPrepass.mVkDescriptorSets[iFramebuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rUiPrepass.mVkPipelineLayout, 0, 2, pUiPrepassSets, 0, nullptr);
		vkCmdDrawIndirect(vkCommandBuffer, gpImGuiManager->mUiPrepassIndirectVkBuffer, static_cast<VkDeviceSize>(iFramebuffer) * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
		gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerUiDepth);
	}

	bool bDebugTextureMode = false;
	if constexpr (kbDebugInput)
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
		// Per-template terrain draws: one indirect draw per IslandTemplate (count fixed at boot
		// from gpIslandTerrain->mIslandCrcsSorted). The stable arena is bound once; each acquired
		// framebuffer's indirect record supplies its template's arena offset and mesh-visible count.
		// Inactive templates have instanceCount=0 → zero draws issued. Subscription changes update
		// only those per-frame records, so the command buffer remains record-once.
		pPipelines[kPipelineTerrain].RecordBindPipelineAndDescriptors(iCommandBuffer, vkCommandBuffer);
		vkCmdBindIndexBuffer(vkCommandBuffer, gpIslands->mIslandMeshArena.mDeviceLocalVkBuffer, 0, VK_INDEX_TYPE_UINT32);
		VkDeviceSize vkVertexOffset = 0;
		vkCmdBindVertexBuffers(vkCommandBuffer, 0, 1, &gpIslands->mIslandMeshArena.mDeviceLocalVkBuffer, &vkVertexOffset);
		for (int64_t iTemplate = 0; iTemplate < gpIslands->miTemplateCount; ++iTemplate)
		{
			vkCmdDrawIndexedIndirect(vkCommandBuffer, gpIslands->mIslandsIndirectVkBuffers.at(iFramebuffer), static_cast<VkDeviceSize>(iTemplate) * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
		}
		gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerTerrain);

		gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWater);
		pPipelines[kPipelineWater].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
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
			if (pPipeline->mFlags & ModelPipelineFlags::kHasTransparentMaterials)
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

		if constexpr (kbDebugRender)
		{
			pPipelines[kPipelineDebugBox].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugSphere].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugCircle].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugLine].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		}
	}

	Texture::RecordEndRenderPass(vkCommandBuffer);
}

void CommandBufferRecordMain::RecordHighDynamicRangeResolve(VkCommandBuffer vkCommandBuffer, int64_t iCommandBuffer, int64_t iFramebuffer)
{
	// HDR resolve: tone-map + color-grade the F16 scene intermediate into the swapchain. Single DONT_CARE
	// color attachment (fully overwritten by the fullscreen quad) — empty flags, no clear.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerHdrResolve);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpSwapchainManager->mVkRenderPass, gpSwapchainManager->mFramebuffers.at(iFramebuffer).presentVkFramebuffer, gpGraphics->mFramebufferExtent2D, VkClearColorValue {}, RenderPassFlags_t {}, VK_SUBPASS_CONTENTS_INLINE);
	gpPipelineManager->mpPipelines[kPipelineHdrResolve].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0);
	Texture::RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerHdrResolve);
}

} // namespace engine

#endif // defined(BT_CLIENT)
