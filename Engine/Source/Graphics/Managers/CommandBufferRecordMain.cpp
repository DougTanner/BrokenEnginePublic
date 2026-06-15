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

	// Pre-compute Gerstner wave displacement + Jacobian normal into two RGBA16F textures so the two
	// Water.vert passes below (WaterSkyboxOne + main Water) can texelFetch a single value per vertex
	// instead of summing iWaterLowCount + iWaterMediumCount waves twice. Dispatch is fixed at LOD0
	// vertex-grid size; the shader bounds-checks each thread against iWaterActiveQuad* and early-returns
	// for inactive cells. Elevation texture was rendered earlier in the Global command buffer
	// (CommandBufferRecordGlobal.cpp ~line 96), which submits before Main per the acquire-Global-Main-ImGui
	// semaphore chain — no extra elevation barrier required.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterDisplacement);
	auto [iWaterDisplacementX, iWaterDisplacementY] = TextureManager::WaterDetailTextureSize(gWaterShapeDetail.Get());
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	pPipelines[kPipelineWaterDisplacement].RecordCompute(iCommandBuffer, vkCommandBuffer, (iWaterDisplacementX + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize, (iWaterDisplacementY + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterDisplacement);

	// WaterSkyboxOne pre-pass: extracts the One-lobe of the water skybox specular into its own MSAA RT
	// at hardcoded 4x sample shading. Runs in the Main command buffer BEFORE the swapchain render pass
	// (line below) so the resolved texture is available when kPipelineWater samples binding 12. The
	// render pass's VK_SUBPASS_EXTERNAL dependency handles the layout-transition + visibility barrier;
	// no manual vkCmdPipelineBarrier is required.
	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterSkyboxOne);
	Texture::RecordBeginRenderPass(vkCommandBuffer, gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneVkRenderPass, gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneVkFramebuffer, {gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneResolveTexture.mInfo.extent.width, gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneResolveTexture.mInfo.extent.height}, VkClearColorValue {0.0f, 0.0f, 0.0f, 0.0f}, RenderPassFlags_t {RenderPassFlags::kClear}, VK_SUBPASS_CONTENTS_INLINE);
	pPipelines[kPipelineWaterSkyboxOne].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, {0.0f, 0.0f, 0.0f, 0.0f});
	Texture::RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerWaterSkyboxOne);

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

	// UI depth pre-pass: depth-only quads at ImGui window positions (instanceCount=0 when disabled)
	{
		Pipeline& rUiPrepass = pPipelines[kPipelineUiDepthPrepass];
		vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rUiPrepass.mVkPipeline);
		VkDescriptorSet pUiPrepassSets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iFramebuffer], rUiPrepass.mVkDescriptorSets[iFramebuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rUiPrepass.mVkPipelineLayout, 0, 2, pUiPrepassSets, 0, nullptr);
		vkCmdDrawIndirect(vkCommandBuffer, gpImGuiManager->mUiPrepassIndirectVkBuffer, static_cast<VkDeviceSize>(iFramebuffer) * sizeof(VkDrawIndirectCommand), 1, sizeof(VkDrawIndirectCommand));
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
		// instanceCount is rewritten each frame in Islands::UpdateActiveIslands. firstInstance is
		// baked at boot to iTemplate*kiMaxPlacementsPerTemplate so Terrain.vert's pQuads[gl_InstanceIndex]
		// lookups land in the right per-template SSBO range. Inactive templates have
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

		if constexpr (kbDebugRender)
		{
			pPipelines[kPipelineDebugBox].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugSphere].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugCircle].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
			pPipelines[kPipelineDebugLine].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
		}
	}

	gpProfileManager->GpuStart(iCommandBuffer, vkCommandBuffer, kGpuTimerText);
	pPipelines[kPipelineProfileText].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerText);

	Texture::RecordEndRenderPass(vkCommandBuffer);
	gpProfileManager->GpuStop(iCommandBuffer, vkCommandBuffer, kGpuTimerImage);

	CHECK_VK(vkEndCommandBuffer(vkCommandBuffer));
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
				.renderArea = {.offset = {0, 0}, .extent = {uiPassWidth, uiPassHeight}},
				.clearValueCount = static_cast<uint32_t>(std::size(pSpreadClearValues)),
				.pClearValues = pSpreadClearValues,
			};
			vkCmdBeginRenderPass(vkCommandBuffer, &vkSpreadRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
			gpPipelineManager->mSpreadPipelines[iPass].RecordDraw(iCommandBuffer, vkCommandBuffer, 1, 0, {static_cast<float>(uiPassWidth), static_cast<float>(uiPassHeight), static_cast<float>(iPass), 0.0f});
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

		uint32_t uiCombineGroupsX = (uiCombineWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		uint32_t uiCombineGroupsY = (uiCombineHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
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
		uint32_t uiTemporalGroupsX = (uiCombineWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
		uint32_t uiTemporalGroupsY = (uiCombineHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
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

} // namespace engine

#endif // defined(BT_CLIENT)
