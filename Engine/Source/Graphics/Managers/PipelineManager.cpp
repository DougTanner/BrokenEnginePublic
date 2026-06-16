#if defined(BT_CLIENT)

#include "Graphics/Managers/PipelineManager.h"

#include "Data/Shader.h"
#include "Data/Texture.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

PipelineManager::PipelineManager()
: mDynamicPipelines(mShaders)
{
	gpPipelineManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerPipelineManager);

	// Load all shaders from pack chunks
	const std::unordered_map<common::crc_t, EagerChunk>& rChunkMap = gpFileManager->GetEagerChunkMap();
	for (const auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kShader))
		{
			continue;
		}

		if constexpr (!kbDebugPrintf)
		{
			if (std::strcmp(rChunk.pHeader->pcPath, "Shaders\\Log.vert") == 0)
			{
				continue;
			}
		}

		const common::ShaderHeader& rShaderHeader = rChunk.pHeader->shaderHeader;
		int64_t iBindingsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rShaderHeader.iDescriptorSetLayoutBindings * static_cast<int64_t>(sizeof(VkDescriptorSetLayoutBinding)));
		int64_t iSetIndicesSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rShaderHeader.iDescriptorSetLayoutBindings * static_cast<int64_t>(sizeof(uint32_t)));
		int64_t iAttrsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rShaderHeader.iVertexInputAttributeDescriptions * static_cast<int64_t>(sizeof(VkVertexInputAttributeDescription)));

		ShaderInfo info
		{
			.pChunkHeader = rChunk.pHeader,
			.pDescriptorBindings = reinterpret_cast<const VkDescriptorSetLayoutBinding*>(rChunk.pData),
			.pDescriptorSetIndices = reinterpret_cast<const uint32_t*>(rChunk.pData + iBindingsSize),
			.pVertexAttributes = reinterpret_cast<const VkVertexInputAttributeDescription*>(rChunk.pData + iBindingsSize + iSetIndicesSize),
			.iSpirvSize = rChunk.pHeader->iSize - iBindingsSize - iSetIndicesSize - iAttrsSize,
		};
		auto [it, bInserted] = mShaders.try_emplace(rCrc, info, rChunk.pData + iBindingsSize + iSetIndicesSize + iAttrsSize);
		ASSERT(bInserted);
	}

	// Generate BRDF LUT texture before creating model pipelines that reference it
	gpTextureManager->mTextureCache.GeneratePbrLutBrdf();

	// Clear stale pipeline pointers before pipelines are recreated
	gpTextureManager->mTextureDescriptors.ClearTextureBindings();

	gpBufferManager->CreateLightingSpreadBuffers();
	CreateLightingPipelines();
	CreateLightingBlurPipelines();
	CreatePipelineShadows();
	CreateLightingShadowDependentPipelines();

	if constexpr (kbDebugPrintf)
	{
		mpPipelines[kPipelineLog].Create(
		{
			.name = "Log",
			.flags = {kRenderTarget, kPushConstants},
			.ppShaders = {&mShaders.at(data::kShadersLogvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLogTexture.mVkRenderPass,
			.vkExtent3D = gpTextureManager->mRenderTargetTextures.mLogTexture.mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			},
		});
	}

	CreateTerrainDataPipelines();

	mpPipelines[kPipelineProfileText].Create(
	{
		.name = "ProfileText",
		.flags = {kIndirectHostVisible, kAlphaBlend, kNoWireframe, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &mShaders.at(data::kShadersUiProfileTextfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mTextStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesUiBC4NotoSansRegularpngCrc},
		},
	});

	mpPipelines[kPipelineUiDepthPrepass].Create(
	{
		.name = "UiDepthPrepass",
		.flags = {kDepthTest, kDepthWrite, kNoColorWrite, kNoWireframe},
		.ppShaders = {&mShaders.at(data::kShadersUiUiDepthPrepassvertCrc), &mShaders.at(data::kShadersUiUiDepthPrepassfragCrc)},
		.pVertexBuffer = nullptr,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mUiRectStorageBuffers.data()},
		},
	});

	if constexpr (kbDebugInput)
	{
		RenderTargetTextures& rTextures = gpTextureManager->mRenderTargetTextures;
		mpPipelines[kPipelineDebugTexture].Create(
		{
			.name = "DebugTexture",
			.flags = {kNoWireframe},
			.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersDebugTexturefragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTextures},
				{.flags = kCombinedSamplers, .iCount = 3, .ppTextures = rTextures.mppLightingDepositTextures},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTexturesB},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTexturesC},
			},
		});
	}

	CreateSmokeWindPipelines();

	CreateParticlePipelines();
	CreateDebugRenderPipelines();

	game::FrameInterpolate::GraphicsResources();
}

PipelineManager::~PipelineManager()
{
	gpPipelineManager = nullptr;
}

void PipelineManager::CreateLightingPipelines()
{
	RenderTargetTextures& rTextures = gpTextureManager->mRenderTargetTextures;

	// Spread pipelines (radial directional spread, fragment shader with MRT)
	// Pass 0 reads deposit textures, passes 1+ read previous pass spread textures
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		mSpreadPipelines[iPass].Destroy();
		mSpreadPipelineNames[iPass] = std::format("LightingSpread{}", iPass);
		mSpreadPipelines[iPass].Create(
		{
			.name = mSpreadPipelineNames[iPass],
			.flags = {kRenderTarget, kPushConstants},
			.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersLightingLightingSpreadfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = rTextures.mSpreadVkRenderPass,
			.vkExtent3D = rTextures.mpSpreadTextures[iPass][0].mInfo.extent,
			.iColorAttachmentCount = 6,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kCombinedSamplers, .iCount = 1, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[0] : &rTextures.mpSpreadTextures[iPass - 1][0]},
				{.flags = kCombinedSamplers, .iCount = 1, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[1] : &rTextures.mpSpreadTextures[iPass - 1][1]},
				{.flags = kCombinedSamplers, .iCount = 1, .pTexture = iPass == 0 ? &rTextures.mpLightingTextures[2] : &rTextures.mpSpreadTextures[iPass - 1][2]},
				{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rTextures.mTerrainElevationTexture},
			},
		});
	}

	// Combine pipeline (tone map spread float16 → UNORM, all 3 colors)
	// Build texture pointer arrays for sampler descriptor arrays (one per color channel, kiMaxSpreadPasses entries each)
	Texture* ppSpreadR[shaders::kiMaxSpreadPasses] {};
	Texture* ppSpreadG[shaders::kiMaxSpreadPasses] {};
	Texture* ppSpreadB[shaders::kiMaxSpreadPasses] {};
	for (int64_t i = 0; i < shaders::kiMaxSpreadPasses; ++i)
	{
		ppSpreadR[i] = &rTextures.mpSpreadOnlyTextures[i][0];
		ppSpreadG[i] = &rTextures.mpSpreadOnlyTextures[i][1];
		ppSpreadB[i] = &rTextures.mpSpreadOnlyTextures[i][2];
	}
	mCombinePipeline.Destroy();
	mCombinePipeline.Create(
	{
		.name = "LightCombine",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mShaders.at(data::kShadersLightingLightCombinecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadR},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadG},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxSpreadPasses, .ppTextures = ppSpreadB},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[0]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[1]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[2]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mAmbientCombineTexture},
		},
	});

	// Temporal pass: 4 history samplers + the 4 combine outputs (read-write storage images), reprojected and
	// EMA-blended in place. No push constants — the shader reads the combine extent via imageSize().
	mLightingTemporalPipeline.Destroy();
	mLightingTemporalPipeline.Create(
	{
		.name = "LightingTemporal",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersLightingLightingTemporalcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rTextures.mpLightingHistoryTextures[0]},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rTextures.mpLightingHistoryTextures[1]},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rTextures.mpLightingHistoryTextures[2]},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rTextures.mAmbientHistoryTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[0]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[1]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mpCombineTextures[2]},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &rTextures.mAmbientCombineTexture},
		},
	});
}

void PipelineManager::CreatePipelineShadows()
{
	mpPipelines[kPipelineShadowElevation].Create(
	{
		.name = "ShadowElevation",
		// kMax: islands' bounding rectangles may overlap (chain packs by hull); MAX-blend the per-island
		// heightmaps so the tallest terrain wins per pixel instead of last-draw-wins. RTT clears to
		// mfSeaFloorElevation (the shared ocean floor, the lowest any heightmap reaches), so single-island
		// pixels are unchanged (max(floor, v) == v).
		.flags = {kRenderTarget, kPushConstants, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// kSamplerElevation: bindless source is R32_SFLOAT; sampler chooses LINEAR or NEAREST per device capability (see TextureManager::CreateSamplers).
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 2 (elevation)
		},
	});

	mpPipelines[kPipelineShadow].Create(
	{
		.name = "Shadow",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersShadowShadowcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowElevationTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowTexture},
		},
	});

	struct ShadowBlurDesc
	{
		Pipelines ePipeline;
		const char* pcName;
		common::crc_t shaderCrc;
		Texture& rInputTexture;
		Texture& rOutputTexture;
	};
	ShadowBlurDesc pShadowBlurDescs[]
	{
		{.ePipeline = kPipelineShadowBlurH, .pcName = "ShadowBlurH", .shaderCrc = data::kShadersShadowShadowBlurHcompCrc, .rInputTexture = gpTextureManager->mRenderTargetTextures.mShadowTexture, .rOutputTexture = gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture},
		{.ePipeline = kPipelineShadowBlurV, .pcName = "ShadowBlurV", .shaderCrc = data::kShadersShadowShadowBlurVcompCrc, .rInputTexture = gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture, .rOutputTexture = gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
		{.ePipeline = kPipelineObjectShadowsBlurH, .pcName = "ObjectShadowsBlurH", .shaderCrc = data::kShadersShadowObjectShadowsBlurHcompCrc, .rInputTexture = gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture, .rOutputTexture = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture},
		{.ePipeline = kPipelineObjectShadowsBlurV, .pcName = "ObjectShadowsBlurV", .shaderCrc = data::kShadersShadowObjectShadowsBlurVcompCrc, .rInputTexture = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurIntermediateTexture, .rOutputTexture = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
	};
	for (const ShadowBlurDesc& rDesc : pShadowBlurDescs)
	{
		mpPipelines[rDesc.ePipeline].Create(
		{
			.name = rDesc.pcName,
			.flags = {kCompute},
			.ppShaders = {&mShaders.at(rDesc.shaderCrc)},
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &rDesc.rInputTexture},
				{.flags = kStorageImages, .iCount = 1, .pTexture = &rDesc.rOutputTexture},
			},
		});
	}

	// Temporal accumulation: gather the reprojected previous-frame shadow (mShadowHistoryTexture) and blend it
	// in place into mShadowBlurTexture, which is then copied back into the history for the next frame.
	mpPipelines[kPipelineShadowTemporal].Create(
	{
		.name = "ShadowTemporal",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersShadowShadowTemporalcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowHistoryTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
		},
	});
}

void PipelineManager::CreateLightingBlurPipelines()
{
	// Lighting texture pre-blur pipelines (descriptors rebound per-texture at blur time)
	mpPipelines[kPipelineLightingBlurH].Create(
	{
		.name = "LightingBlurH",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mShaders.at(data::kShadersLightingLightingBlurHcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mWhiteTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mpCombineTextures[0]},
		},
	});
	mpPipelines[kPipelineLightingBlurV].Create(
	{
		.name = "LightingBlurV",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mShaders.at(data::kShadersLightingLightingBlurVcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mWhiteTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mpCombineTextures[0]},
		},
	});
}

void PipelineManager::CreateLightingShadowDependentPipelines()
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
		.ppShaders = {&mShaders.at(data::kShadersTerrainTerrainvertCrc), &mShaders.at(data::kShadersTerrainTerrainfragCrc)},
		// pVertexBuffer is null: vertex buffer is per-island and bound at draw time. Vertex input
		// stride and attribute layout come from shader reflection (Terrain.vert declares vec3 in).
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = {kCombinedSamplers, kSamplerBorderWhite}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			// Bindless per-island color / normal / AO arrays (was previously single composite RTTs).
			// Compositing fragment shader indexes these with the per-instance `uiTextureSlot` forwarded
			// from Terrain.vert; `kSamplerClamp` matches the per-slot RegisterTextureBinding flag in
			// IslandTerrain.cpp so descriptor writes line up with the sampler descriptor layout.
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mColorTextures.data()}, // set=1 binding 6 (color)
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mNormalsTextures.data()}, // set=1 binding 7 (normals)
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data()}, // set=1 binding 8 (ambient occlusion)
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7Rock0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal1pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal2pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandpngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal1jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal2jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal4jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
			// AxisAlignedQuadLayout instance buffer used by Terrain.vert to transform island-local
			// mesh vertices into world space (set=1 binding=19). Mirrors kPipelineShadowElevation's
			// SSBO usage; gl_InstanceIndex is supplied per-island via firstInstance at draw time.
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// Bindless per-island material masks (set=1 binding=20). Packed RGBA = Rock/Sand/Snow/Flow
			// replacing the procedural fRockPercent / fBeachPercent / fSnowPercent heuristics in
			// Terrain.frag. Appended after the SSBO so existing frag bindings 9..18 and the
			// Terrain.vert SSBO at 19 stay put.
			{.flags = {kCombinedSamplers, kSamplerClamp, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mMasksTextures.data()}, // set=1 binding 20 (masks)
			// Per-island heightmap array (R32_SFLOAT), set=1 binding 21 (appended after masks at 20 so bindings
			// 0..20 stay put). Terrain.vert samples it at the island-local UV to sink THIS island's submerged
			// verts (own elevation < zero-out) to the flat sea floor, so an overlapping neighbor's MAX-composite
			// height never lifts this island's underwater mesh. Same array pointer the prepasses consume, so the
			// existing per-slot RegisterTextureBinding / eviction machinery patches this binding automatically.
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 21 (Terrain.vert own-heightmap sink)
		},
	});

	// Pre-computes Gerstner wave displacement + Jacobian normal once per frame into two RGBA16F
	// textures sampled by Water.vert (kPipelineWater + kPipelineWaterSkyboxOne) — eliminates the
	// duplicate wave sum the two passes used to evaluate via the now-removed GerstnerLow/Medium
	// helpers. Dispatch is sized to the LOD0 vertex grid at CB-record time; the shader's
	// iWaterActiveQuad* uniform bounds-checks each thread so smaller LODs early-return.
	mpPipelines[kPipelineWaterDisplacement].Create(
	{
		.name = "WaterDisplacement",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersWaterWaterDisplacementcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture},
		},
	});

	// Water
	// kSampleShading was dropped here: the high-power One lobe of the skybox specular was extracted into
	// kPipelineWaterSkyboxOne (forced 4x MSAA + full sample shading, resolved to mWaterSkyboxOneResolveTexture).
	// The Two/Three lobes remain inlined here without sample shading because their power is low enough to
	// alias acceptably. Main water samples the resolved One-lobe RT in screen space at binding 12.
	PipelineInfo waterInfo
	{
		.name = "Water",
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kUpdateAfterBind, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersWaterWatervertCrc), &mShaders.at(data::kShadersWaterWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
	};
	FillWaterSharedDescriptors(waterInfo.pDescriptorInfos);
	// binding 12: resolved One-lobe skybox specular RT (Water-only; kPipelineWaterSkyboxOne outputs this target).
	waterInfo.pDescriptorInfos[12] = {.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneResolveTexture};
	// Compute-pre-computed Gerstner displacement + normal sampled in Water.vert via texelFetch.
	// Explicit bindings 13/14: kPipelineWaterSkyboxOne has no binding-12 descriptor (its frag shader doesn't
	// sample the resolve target — it's the OUTPUT). Explicit binding keeps the shader-side binding numbers
	// identical across both pipelines.
	waterInfo.pDescriptorInfos[13] = {.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = 13, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture};
	waterInfo.pDescriptorInfos[14] = {.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = 14, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture};
	mpPipelines[kPipelineWater].Create(waterInfo);

	// WaterSkyboxOne: pre-pass at hardcoded 4x MSAA + full sample shading rendering only the One-lobe
	// of the water skybox specular. Same vertex shader / mesh / indirect draw as Water so the screen-space
	// sample in Water.frag aligns. Output resolves into mWaterSkyboxOneResolveTexture, bound above at
	// binding 12 of the main water pipeline.
	//
	// Descriptor layout MIRRORS the main water pipeline exactly (bindings 0-11) because the shared
	// Water.vert reads terrain elevation at set=1 binding=5 — any divergence in binding numbers would
	// give the vertex shader garbage elevation and trigger its over-land early-out, producing flat
	// (un-Gerstner-displaced) geometry. The new fragment shader uses only bindings 0/1/5/6/8; the
	// other slots are present but unread (descriptor write is required, sample is not).
	PipelineInfo skyboxInfo
	{
		.name = "WaterSkyboxOne",
		.flags = {kCullBack, kRenderTarget, kForceFullSampleShading4x, kUpdateAfterBind, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersWaterWatervertCrc), &mShaders.at(data::kShadersWaterWaterSkyboxOnefragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWaterSkyboxOneResolveTexture.mInfo.extent,
	};
	FillWaterSharedDescriptors(skyboxInfo.pDescriptorInfos);
	// Explicit bindings 13/14 occupy indices 12/13 — this pipeline has no binding-12 descriptor (it OUTPUTs the
	// resolve target). Both pipelines share Water.vert, so the shader-side binding numbers must agree.
	skyboxInfo.pDescriptorInfos[12] = {.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = 13, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture};
	skyboxInfo.pDescriptorInfos[13] = {.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = 14, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture};
	mpPipelines[kPipelineWaterSkyboxOne].Create(skyboxInfo);
}

// Bindings 0-11 shared verbatim by kPipelineWater and kPipelineWaterSkyboxOne. Both bind Water.vert, which
// reads terrain elevation at set=1 binding=5 — divergence here silently flattens the water (the vertex shader's
// over-land early-out fires on garbage elevation). Single-sourced so the invariant is structural, not
// comment-enforced. Callers append the pipeline-specific tail.
void PipelineManager::FillWaterSharedDescriptors(DescriptorInfo* pDescriptorInfos)
{
	pDescriptorInfos[0]  = {.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()};
	pDescriptorInfos[1]  = {.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()};
	pDescriptorInfos[2]  = {.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures};
	pDescriptorInfos[3]  = {.flags = {kCombinedSamplers, kSamplerBorderWhite}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture};
	pDescriptorInfos[4]  = {.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture};
	pDescriptorInfos[5]  = {.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture};
	pDescriptorInfos[6]  = {.flags = kCombinedSamplers, .iCount = 1, .textureCrc = TextureManager::kPrefilteredWaterCrc};
	pDescriptorInfos[7]  = {.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc};
	pDescriptorInfos[8]  = {.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = TextureManager::kiWaterNormalCount, .ppTextures = mppWaterNormalTextures};
	pDescriptorInfos[9]  = {.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc};
	pDescriptorInfos[10] = {.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne};
	pDescriptorInfos[11] = {.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture};
}

void PipelineManager::CreateTerrainDataPipelines()
{
	mpPipelines[kPipelineTerrainElevation].Create(
	{
		.name = "TerrainElevation",
		// kMax: see kPipelineShadowElevation — MAX-blend overlapping islands' heightmaps so the tallest
		// terrain wins per pixel. RTT clears to mfSeaFloorElevation, so single-island pixels are unchanged.
		.flags = {kRenderTarget, kPushConstants, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// kSamplerElevation for R32_SFLOAT bindless heightmap (LINEAR/NEAREST per device capability). See TextureManager::CreateSamplers.
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 2 (elevation, kPipelineTerrainElevation)
		},
	});
}

void PipelineManager::CreateSmokeWindPipelines()
{
	mpPipelines[kPipelineSmokeClearA].Create(
	{
		.name = "SmokeClearA",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
	mpPipelines[kPipelineSmokeClearB].Create(
	{
		.name = "SmokeClearB",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});

	gpBufferManager->CreateSmokeHierarchicalBuffers();

	mpPipelines[kPipelineSmokeOccupancyDilate].Create(
	{
		.name = "SmokeOccupancyDilate",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeOccupancyDilatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
		},
	});

	mpPipelines[kPipelineSmokeOccupancyDilateRemap].Create(
	{
		.name = "SmokeOccupancyDilateRemap",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeOccupancyDilateRemapcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
		},
	});

	mpPipelines[kPipelineSmokeSpreadComputeB].Create(
	{
		.name = "SmokeSpreadComputeB",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeSpreadTwocompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffer},
		},
	});

	mpPipelines[kPipelineSmokeSpreadComputeA].Create(
	{
		.name = "SmokeSpreadComputeA",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeSpreadOnecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffer},
		},
	});

	gpBufferManager->CreateWindHierarchicalBuffers();

	// Wind occupancy dilate A: reads OccupancyB, writes ActiveTileA
	mpPipelines[kPipelineWindOccupancyDilateA].Create(
	{
		.name = "WindOccupancyDilateA",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersWindWindOccupancyDilatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindOccupancyVkBuffers[1]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindActiveTileVkBuffers[0]},
		},
	});

	// Wind occupancy dilate B: reads OccupancyA, writes ActiveTileB
	mpPipelines[kPipelineWindOccupancyDilateB].Create(
	{
		.name = "WindOccupancyDilateB",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersWindWindOccupancyDilatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindOccupancyVkBuffers[0]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindActiveTileVkBuffers[1]},
		},
	});

	// Wind spread compute A: reads TextureTwo, writes TextureOne, uses ActiveTileA + OccupancyA
	mpPipelines[kPipelineWindSpreadComputeA].Create(
	{
		.name = "WindSpreadComputeA",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersWindWindSpreadOnecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindActiveTileVkBuffers[0]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindOccupancyVkBuffers[0]},
		},
	});

	// Wind spread compute B: reads TextureOne, writes TextureTwo, uses ActiveTileB + OccupancyB
	mpPipelines[kPipelineWindSpreadComputeB].Create(
	{
		.name = "WindSpreadComputeB",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersWindWindSpreadTwocompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindActiveTileVkBuffers[1]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mWindOccupancyVkBuffers[1]},
		},
	});
}

void PipelineManager::CreateParticlePipelines()
{
	mpPipelines[kPipelineLongParticlesUpdate].Create(
	{
		.name = "LongParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineLongParticlesRender].Create(
	{
		.name = "LongParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersParticlesLongParticlesRendervertCrc), &mShaders.at(data::kShadersParticlesParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelineLongParticlesSpawn].Create(
	{
		.name = "LongParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesSpawncompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mLongParticlesSpawnStorageBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineLongParticlesUpdate].mIndirectVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineLongParticlesRender].mIndirectVkBuffer},
		},
	});

	mpPipelines[kPipelineSquareParticlesUpdate].Create(
	{
		.name = "SquareParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineSquareParticlesRender].Create(
	{
		.name = "SquareParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersParticlesSquareParticlesRendervertCrc), &mShaders.at(data::kShadersParticlesParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelineSquareParticlesSpawn].Create(
	{
		.name = "SquareParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesSpawncompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSquareParticlesSpawnStorageBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineSquareParticlesUpdate].mIndirectVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineSquareParticlesRender].mIndirectVkBuffer},
		},
	});
}

void PipelineManager::CreateDebugRenderPipelines()
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

	using enum DescriptorFlags;
	using enum PipelineFlags;

	struct DebugRenderPipelineEntry
	{
		Pipelines ePipeline;
		std::string_view name;
		common::crc_t crc;
		Buffer* pVertexBuffer;
		common::crc_t vertexShaderCrc;
	};

	DebugRenderPipelineEntry pEntries[]
	{
		{kPipelineDebugBox,    "DebugBox",    common::CrcConsteval("DebugBox"),    &gpBufferManager->mDebugBoxVertexBuffer,    data::kShadersDebugDebugRendervertCrc},
		{kPipelineDebugSphere, "DebugSphere", common::CrcConsteval("DebugSphere"), &gpBufferManager->mDebugSphereVertexBuffer, data::kShadersDebugDebugRendervertCrc},
		{kPipelineDebugCircle, "DebugCircle", common::CrcConsteval("DebugCircle"), &gpBufferManager->mDebugCircleVertexBuffer, data::kShadersDebugDebugRenderBillboardvertCrc},
		{kPipelineDebugLine,   "DebugLine",   common::CrcConsteval("DebugLine"),   &gpBufferManager->mDebugLineVertexBuffer,   data::kShadersDebugDebugRendervertCrc},
	};

	for (const DebugRenderPipelineEntry& rEntry : pEntries)
	{
		gpBufferManager->CreateDynamicBuffer(rEntry.crc, kBufferMain, rEntry.name, sizeof(shaders::DebugRenderLayout));

		mpPipelines[rEntry.ePipeline].Create(
		{
			.name = rEntry.name,
			.flags = {kIndirectHostVisible, kLineList, kAlphaBlend, kUpdateAfterBind},
			.ppShaders = {&mShaders.at(rEntry.vertexShaderCrc), &mShaders.at(data::kShadersDebugDebugRenderfragCrc)},
			.pVertexBuffer = rEntry.pVertexBuffer,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(rEntry.crc).data()},
			},
		});
	}
}

void PipelineManager::VerifyAllDescriptorGenerations()
{
	// muiGeneration == 0 means the Texture is still lazy (never Created — descriptor holds the
	// placeholder view installed by InitDeferred). That's a valid pre-load state, not staleness.
	// After Create, gen >= 1; mVkImage going null then means destroyed-without-recreate, which IS stale.
	for (const auto& [rCrc, rBindings] : gpTextureManager->mTextureDescriptors.mTextureBindings)
	{
		for (const TextureDescriptors::TextureBinding& rBinding : rBindings)
		{
			if (rBinding.pTexture != nullptr && rBinding.pTexture->muiGeneration != 0
				&& (rBinding.pTexture->muiGeneration != rBinding.uiTextureGeneration || rBinding.pTexture->mVkImage == VK_NULL_HANDLE))
			{
				LOG(kGraphics, kError, "Descriptor staleness: pipeline={} binding={} crc={} texture={} snapshotGen={} currentGen={} vkImage={}", rBinding.pPipeline->mInfo.name, rBinding.iBinding, rCrc, reinterpret_cast<uintptr_t>(rBinding.pTexture), rBinding.uiTextureGeneration, rBinding.pTexture->muiGeneration, reinterpret_cast<uintptr_t>(rBinding.pTexture->mVkImage));
				DEBUG_BREAK();
			}

			int64_t iCount = static_cast<int64_t>(rBinding.textures.size());
			ASSERT(static_cast<int64_t>(rBinding.uiTextureGenerations.size()) == iCount);
			// Per-island-slot bindings (iArrayIndex >= 0) own and keep current only element iArrayIndex;
			// WriteArrayBindingDescriptors touches only that slot, leaving the rest of the snapshot frozen
			// at registration time. Those frozen elements legitimately go stale as neighbouring slots evict
			// (Texture::Destroy nulls mVkImage without bumping the generation), so verifying them is a false
			// positive. Full-array bindings (iArrayIndex < 0, e.g. water normals) still verify every element.
			int64_t iBegin = rBinding.iArrayIndex >= 0 ? rBinding.iArrayIndex : 0;
			int64_t iEnd = rBinding.iArrayIndex >= 0 ? rBinding.iArrayIndex + 1 : iCount;
			for (int64_t i = iBegin; i < iEnd; ++i)
			{
				Texture* pTexture = rBinding.textures.at(i);
				if (pTexture == nullptr || pTexture->muiGeneration == 0)
				{
					continue;
				}
				if (pTexture->muiGeneration != rBinding.uiTextureGenerations.at(i) || pTexture->mVkImage == VK_NULL_HANDLE)
				{
					LOG(kGraphics, kError, "Descriptor staleness (array): pipeline={} binding={} crc={} slot={} texture={} snapshotGen={} currentGen={} vkImage={}", rBinding.pPipeline->mInfo.name, rBinding.iBinding, rCrc, i, reinterpret_cast<uintptr_t>(pTexture), rBinding.uiTextureGenerations.at(i), pTexture->muiGeneration, reinterpret_cast<uintptr_t>(pTexture->mVkImage));
					DEBUG_BREAK();
				}
			}
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
