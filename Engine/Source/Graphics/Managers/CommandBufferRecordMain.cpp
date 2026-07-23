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

	VkClearValue pClearValues[3] {};
	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.framebuffer = gpTextureManager->mRenderTargetTextures.mLightingVkFramebuffer,
		.renderArea = {.offset = {0, 0}, .extent = {gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height}},
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
			// Draw indirect so RenderLightingSpreadIndirect can drop the instance count to 0 on a frame with no
			// light deposit, without re-recording this CB. Begin/end and the clear stay unconditional: the
			// pSpreadClearValues LOAD_OP_CLEAR still zeroes all 6 attachments, which is exactly what the gather
			// over an all-zero deposit would have written.
			vkCmdBeginRenderPass(vkCommandBuffer, &vkSpreadRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			gpPipelineManager->mSpreadPipelines[iPass].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {static_cast<float>(uiPassWidth), static_cast<float>(uiPassHeight), static_cast<float>(iPass), 0.0f});
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

	{
		Pipeline& rCombinePipeline = gpPipelineManager->mCombinePipeline;
		int64_t iDescriptorSetIndex = rCombinePipeline.mbPerCommandBuffer ? iCommandBuffer : 0;
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rCombinePipeline.mVkPipeline);
		BindComputeDescriptorSets(vkCommandBuffer, rCombinePipeline.mVkPipelineLayout, rCombinePipeline.mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, rCombinePipeline.mVkDescriptorSets);

		uint32_t uiCombineWidth = rRenderTargetTextures.mpCombineTextures[0].mInfo.extent.width;
		uint32_t uiCombineHeight = rRenderTargetTextures.mpCombineTextures[0].mInfo.extent.height;
		shaders::CombinePushConstantsLayout combinePushConstants
		{
			.uiWidth = uiCombineWidth,
			.uiHeight = uiCombineHeight,
		};

		uint32_t uiCombineGroupsX = TileCount(uiCombineWidth);
		uint32_t uiCombineGroupsY = TileCount(uiCombineHeight);
		vkCmdPushConstants(vkCommandBuffer, rCombinePipeline.mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(shaders::CombinePushConstantsLayout), &combinePushConstants);
		vkCmdDispatch(vkCommandBuffer, uiCombineGroupsX, uiCombineGroupsY, 1);
	}

	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingCombine);

	// Phase 3: Temporal accumulation — reproject + EMA-blend the previous frame's combine into the 4 combine outputs
	// in place, then copy the blended result into the 4 history textures for next frame. De-flickers the texel-ramp
	// resample (mirror of the shadow temporal pass). Runs unconditionally every frame even when gLightingTemporalBlend
	// == 1.0 (disabled): the mix() is then a no-op but the dispatch + copies still execute (a recorded copy can't be
	// indirect-gated — the region is fixed at record time — and gating would need a destroy-tier CB re-record on the
	// enable/disable edge; blend flows purely through the uniform). Same-layout self-transition: barrier so combine's
	// writes finish before temporal reads them in place.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerLightingTemporal);
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadOnly);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadOnly);

	{
		uint32_t uiCombineWidth = rRenderTargetTextures.mpCombineTextures[0].mInfo.extent.width;
		uint32_t uiCombineHeight = rRenderTargetTextures.mpCombineTextures[0].mInfo.extent.height;
		uint32_t uiTemporalGroupsX = TileCount(uiCombineWidth);
		uint32_t uiTemporalGroupsY = TileCount(uiCombineHeight);
		gpPipelineManager->mLightingTemporalPipeline.RecordCompute(iCommandBuffer, vkCommandBuffer, uiTemporalGroupsX, uiTemporalGroupsY);
	}

	// Copy the blended combine outputs into the history textures for next frame.
	for (int64_t iColor = 0; iColor < 3; ++iColor)
	{
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kTransferSource);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kTransferDestination);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].RecordCopyImageFrom(vkCommandBuffer, rRenderTargetTextures.mpCombineTextures[iColor]);
		rRenderTargetTextures.mpCombineTextures[iColor].TransitionImageLayout(vkCommandBuffer, kTransferSource, kShaderReadOnly);
		rRenderTargetTextures.mpLightingHistoryTextures[iColor].TransitionImageLayout(vkCommandBuffer, kTransferDestination, kShaderReadOnly);
	}
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kTransferSource);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadOnly, kTransferDestination);
	rRenderTargetTextures.mAmbientHistoryTexture.RecordCopyImageFrom(vkCommandBuffer, rRenderTargetTextures.mAmbientCombineTexture);
	rRenderTargetTextures.mAmbientCombineTexture.TransitionImageLayout(vkCommandBuffer, kTransferSource, kShaderReadOnly);
	rRenderTargetTextures.mAmbientHistoryTexture.TransitionImageLayout(vkCommandBuffer, kTransferDestination, kShaderReadOnly);
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
		// from gpIslandTerrain->mIslandCrcsSorted). Each template's Gaea2 Mesher mesh is bound,
		// and vkCmdDrawIndexedIndirect reads the per-template VkDrawIndexedIndirectCommand whose
		// instanceCount is rewritten each frame to Islands::UpdateActiveIslands' mesh-visible prefix.
		// firstInstance is baked at boot to iTemplate*kiMaxPlacementsPerTemplate so Terrain.vert's
		// pQuads[gl_InstanceIndex] lookups land in the right per-template SSBO range. Inactive templates have
		// instanceCount=0 → zero draws issued. Record-once: CB never needs re-record on
		// subscription changes.
		pPipelines[kPipelineTerrain].RecordBindPipelineAndDescriptors(iCommandBuffer, vkCommandBuffer);
		for (int64_t iTemplate = 0; iTemplate < gpIslands->miTemplateCount; ++iTemplate)
		{
			IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(gpIslandTerrain->mIslandCrcsSorted[static_cast<size_t>(iTemplate)]);
			ASSERT(rTemplate.mMeshBuffer.mDeviceLocalVkBuffer != VK_NULL_HANDLE);
			rTemplate.mMeshBuffer.RecordBindVertexBuffer(vkCommandBuffer);
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
