#if defined(BT_CLIENT)

#include "Graphics/Managers/WorldLightingShadowPipelines.h"

#include "Data/Shader.h"
#include "Data/Texture.h"
#include "Graphics/Managers/PipelineManager.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

WorldLightingShadowPipelines::WorldLightingShadowPipelines(std::unordered_map<common::crc_t, Shader>& rShaders, Pipeline* pPipelines, Pipeline* pSpreadPipelines, std::string* pSpreadPipelineNames, Pipeline& rCombinePipeline, Pipeline& rLightingTemporalPipeline, Pipeline& rLightingHistoryCopyPipeline, Texture** ppWaterNormalTextures)
: mrShaders(rShaders)
, mpPipelines(pPipelines)
, mpSpreadPipelines(pSpreadPipelines)
, mpSpreadPipelineNames(pSpreadPipelineNames)
, mrCombinePipeline(rCombinePipeline)
, mrLightingTemporalPipeline(rLightingTemporalPipeline)
, mrLightingHistoryCopyPipeline(rLightingHistoryCopyPipeline)
, mppWaterNormalTextures(ppWaterNormalTextures)
{
}

void WorldLightingShadowPipelines::CreateLightingPipelines()
{
	RenderTargetTextures& rTextures = gpTextureManager->mRenderTargetTextures;

	// Spread pipelines (radial directional spread, fragment shader with MRT)
	// Pass 0 reads deposit textures, passes 1+ read previous pass spread textures
	// kIndirectHostVisible gives each pass a per-framebuffer VkDrawIndexedIndirectCommand slot so the refresh
	// predicate can suppress the chain without re-recording the Main CB. The flag also defers the Pipeline::Create
	// texture request to the first WriteIndirectBuffer with instances, which is a no-op here: these pipelines bind
	// only render-target textures, never disk-loaded chunks.
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		mpSpreadPipelineNames[iPass] = std::format("LightingSpread{}", iPass);
		mpSpreadPipelines[iPass].Create(
		{
			.name = mpSpreadPipelineNames[iPass],
			.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
			.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mrShaders.at(data::kShadersLightingLightingSpreadfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = rTextures.mSpreadVkRenderPass,
			.vkExtent3D = rTextures.mpSpreadTextures[iPass][0].mInfo.extent,
			.iColorAttachmentCount = 6,
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kCombinedSamplers, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[0] : &rTextures.mpSpreadTextures[iPass - 1][0]},
				{.flags = kCombinedSamplers, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[1] : &rTextures.mpSpreadTextures[iPass - 1][1]},
				{.flags = kCombinedSamplers, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[2] : &rTextures.mpSpreadTextures[iPass - 1][2]},
				{.flags = kCombinedSamplers, .pTexture = &rTextures.mTerrainElevationTexture},
			},
		});
	}

	// Combine pipeline (tone map spread float16 → UNORM, all 3 colors)
	// Build texture pointer arrays for sampler descriptor arrays (one per color channel, kiMaxSpreadPasses entries each)
	Texture* ppSpreadRed[shaders::kiMaxSpreadPasses] {};
	Texture* ppSpreadGreen[shaders::kiMaxSpreadPasses] {};
	Texture* ppSpreadBlue[shaders::kiMaxSpreadPasses] {};
	for (int64_t i = 0; i < shaders::kiMaxSpreadPasses; ++i)
	{
		ppSpreadRed[i] = &rTextures.mpSpreadOnlyTextures[i][0];
		ppSpreadGreen[i] = &rTextures.mpSpreadOnlyTextures[i][1];
		ppSpreadBlue[i] = &rTextures.mpSpreadOnlyTextures[i][2];
	}
	mrCombinePipeline.Create(
	{
		.name = "LightCombine",
		.flags = {kCompute, kIndirectHostVisible},
		.ppShaders = {&mrShaders.at(data::kShadersLightingLightCombinecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadRed},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadGreen},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadBlue},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[0]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[1]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[2]},
			{.flags = kStorageImages, .pTexture = &rTextures.mAmbientCombineTexture},
		},
	});

	// Temporal pass: 4 history samplers + the 4 combine outputs (read-write storage images), reprojected and
	// EMA-blended in place. No push constants — the shader reads the combine extent via imageSize().
	mrLightingTemporalPipeline.Create(
	{
		.name = "LightingTemporal",
		.flags = {kCompute, kIndirectHostVisible},
		.ppShaders = {&mrShaders.at(data::kShadersLightingLightingTemporalcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &rTextures.mpLightingHistoryTextures[0]},
			{.flags = kCombinedSamplers, .pTexture = &rTextures.mpLightingHistoryTextures[1]},
			{.flags = kCombinedSamplers, .pTexture = &rTextures.mpLightingHistoryTextures[2]},
			{.flags = kCombinedSamplers, .pTexture = &rTextures.mAmbientHistoryTexture},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[0]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[1]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[2]},
			{.flags = kStorageImages, .pTexture = &rTextures.mAmbientCombineTexture},
		},
	});

	mrLightingHistoryCopyPipeline.Create(
	{
		.name = "LightingHistoryCopy",
		.flags = {kCompute, kIndirectHostVisible},
		.ppShaders = {&mrShaders.at(data::kShadersLightingLightingHistoryCopycompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[0]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[1]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpCombineTextures[2]},
			{.flags = kStorageImages, .pTexture = &rTextures.mAmbientCombineTexture},
			{.flags = kStorageImages, .pTexture = &rTextures.mpLightingHistoryTextures[0]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpLightingHistoryTextures[1]},
			{.flags = kStorageImages, .pTexture = &rTextures.mpLightingHistoryTextures[2]},
			{.flags = kStorageImages, .pTexture = &rTextures.mAmbientHistoryTexture},
		},
	});
}

void WorldLightingShadowPipelines::CreatePipelineShadows()
{
	mpPipelines[kPipelineShadowElevation].Create(
	{
		.name = "ShadowElevation",
		// kMax: islands' bounding rectangles may overlap (chain packs by hull); MAX-blend the per-island
		// heightmaps so the tallest terrain wins per pixel instead of last-draw-wins. RTT clears to
		// mfSeaFloorElevation (the shared ocean floor, the lowest any heightmap reaches), so single-island
		// pixels are unchanged (max(floor, v) == v).
		.flags = {kRenderTarget, kPushConstants, kMax, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// kSamplerElevation: bindless source is R16_SFLOAT; sampler is unconditionally LINEAR (spec-mandated for 16-bit-float formats, see TextureManager::CreateSamplers).
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 2 (elevation)
		},
	});

	mpPipelines[kPipelineShadow].Create(
	{
		.name = "Shadow",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowShadowcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowElevationTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowTexture},
		},
	});

	mpPipelines[kPipelineShadowBlurH].Create(
	{
		.name = "ShadowBlurH",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowShadowBlurHcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture},
		},
	});

	mpPipelines[kPipelineShadowBlurV].Create(
	{
		.name = "ShadowBlurV",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowShadowBlurVcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
		},
	});

	mpPipelines[kPipelineObjectShadowsBlurH].Create(
	{
		.name = "ObjectShadowsBlurH",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowObjectShadowsBlurHcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture},
		},
	});

	mpPipelines[kPipelineObjectShadowsBlurV].Create(
	{
		.name = "ObjectShadowsBlurV",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowObjectShadowsBlurVcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
		},
	});

	// Temporal accumulation: gather the reprojected previous-frame shadow and blend it in place into
	// mShadowBlurTexture. ShadowHistoryCopy then refreshes the distinct history image over the final window.
	mpPipelines[kPipelineShadowTemporal].Create(
	{
		.name = "ShadowTemporal",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowShadowTemporalcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
		},
	});

	mpPipelines[kPipelineShadowHistoryCopy].Create(
	{
		.name = "ShadowHistoryCopy",
		.flags = {kCompute},
		.ppShaders = {&mrShaders.at(data::kShadersShadowShadowHistoryCopycompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture},
		},
	});
}

void WorldLightingShadowPipelines::CreateLightingBlurPipelines()
{
	// Lighting texture pre-blur pipelines (descriptors rebound per-texture at blur time)
	mpPipelines[kPipelineLightingBlurH].Create(
	{
		.name = "LightingBlurH",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mrShaders.at(data::kShadersLightingLightingBlurHcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mWhiteTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mpCombineTextures[0]},
		},
	});

	mpPipelines[kPipelineLightingBlurV].Create(
	{
		.name = "LightingBlurV",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mrShaders.at(data::kShadersLightingLightingBlurVcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mWhiteTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mpCombineTextures[0]},
		},
	});
}

void WorldLightingShadowPipelines::CreateLightingShadowDependentPipelines()
{
	// Resolve the water normal atlas Texture* pointers from kpWaterNormalCrcs. mTextureMap (unordered_map) is pointer-stable.
	for (int64_t i = 0; i < TextureManager::kiWaterNormalCount; ++i)
	{
		mppWaterNormalTextures[i] = &gpTextureManager->mTextureMap.at(TextureManager::kpWaterNormalCrcs[i]);
	}

	// Terrain
	mpPipelines[kPipelineTerrain].Create(
	{
		.name = "Terrain",
		// kIndirect* flag dropped: terrain records one vkCmdDrawIndexedIndirect per island template in
		// CommandBufferRecordMain, from Islands' own indirect buffers (instead of a single visible-area indirect draw).
		.flags = {kDepthTest, kDepthWrite, kCullBack, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersTerrainTerrainvertCrc), &mrShaders.at(data::kShadersTerrainTerrainfragCrc)},
		// pVertexBuffer is null: vertex buffer is per-island and bound at draw time. Vertex input
		// stride and attribute layout come from shader reflection (Terrain.vert declares vec2 f2InPosition).
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = {kCombinedSamplers, kSamplerBorderWhite}, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			// Bindless per-island color / normal / AO arrays.
			// Compositing fragment shader indexes these with the per-instance `uiTextureSlot` forwarded
			// from Terrain.vert; `kSamplerClamp` matches the per-slot RegisterTextureBinding flag in
			// IslandTerrainResidency.cpp so descriptor writes line up with the sampler descriptor layout.
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mColorTextures.data()}, // set=1 binding 6 (color)
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mNormalsTextures.data()}, // set=1 binding 7 (normals)
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data()}, // set=1 binding 8 (ambient occlusion)
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC7Rock0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5SandNormal0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5SandNormal1pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5SandNormal2pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC7SandpngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5RockNormal1jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5RockNormal2jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesTerrainBC5RockNormal4jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
			// AxisAlignedQuadLayout instance buffer used by Terrain.vert to transform island-local
			// mesh vertices into world space (set=1 binding=19). Mirrors kPipelineShadowElevation's
			// SSBO usage; gl_InstanceIndex is supplied per-island via firstInstance at draw time.
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// Bindless per-island material masks (set=1 binding=20). Packed RGBA = Rock/Sand/Snow/Flow
			// replacing the procedural fRockPercent / fBeachPercent / fSnowPercent heuristics in
			// Terrain.frag. Appended after the SSBO so existing frag bindings 9..18 and the
			// Terrain.vert SSBO at 19 stay put.
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mMasksTextures.data()}, // set=1 binding 20 (masks)
			// Per-island heightmap array (R16_SFLOAT), set=1 binding 21 (appended after masks at 20 so bindings
			// 0..20 stay put). Terrain.vert samples it at the island-local UV to sink THIS island's submerged
			// verts (own elevation < zero-out) to the flat sea floor, so an overlapping neighbor's MAX-composite
			// height never lifts this island's underwater mesh. Same array pointer the prepasses consume, so the
			// existing per-slot RegisterTextureBinding / eviction machinery patches this binding automatically.
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 21 (Terrain.vert own-heightmap sink)
		},
	});

	// Pre-computes Gerstner wave displacement + Jacobian normal once per frame into two RGBA16F
	// textures sampled by Water.vert (kPipelineWater) — the vertex shader texelFetches one value per
	// vertex instead of summing the wave bands itself. Dispatch dims are written per frame by MainUniforms (WriteIndirectComputeBuffer) to cover
	// only the active LOD sub-region; the shader's iWaterActiveQuad* uniform still bounds-checks each thread
	// as a defensive guard.
	mpPipelines[kPipelineWaterDisplacement].Create(
	{
		.name = "WaterDisplacement",
		.flags = {kCompute, kIndirectHostVisible},
		.ppShaders = {&mrShaders.at(data::kShadersWaterWaterDisplacementcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture},
		},
	});

	// Water. All three skybox-specular lobes are evaluated inline with analytic specular AA
	// (WATER_SPEC_AA_MODE in Water.frag) instead of MSAA sample shading — kSampleShading stays off.
	mpPipelines[kPipelineWater].Create(
	{
		.name = "Water",
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kUpdateAfterBind, kIndirectHostVisible},
		.ppShaders = {&mrShaders.at(data::kShadersWaterWatervertCrc), &mrShaders.at(data::kShadersWaterWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = {kCombinedSamplers, kSamplerBorderWhite}, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .textureCrc = TextureManager::kPrefilteredWaterCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .textureCrc = data::kTexturesWaterBC4NoisepngCrc},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeatWater}, .iCount = TextureManager::kiWaterNormalCount, .ppTextures = mppWaterNormalTextures},
			{.flags = kCombinedSamplers, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
			// Compute-pre-computed Gerstner displacement + normal sampled in Water.vert via texelFetch.
			// Explicit bindings keep the shader-side numbers at 13/14 — binding 12 is intentionally
			// unused; renumbering the displacement bindings isn't worth the churn.
			{.flags = kCombinedSamplers, .iExplicitBinding = shaders::kiWaterBindingDisplacement, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture},
			{.flags = kCombinedSamplers, .iExplicitBinding = shaders::kiWaterBindingDisplacementNormal, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture},
		},
	});
}

} // namespace engine

#endif // defined(BT_CLIENT)
