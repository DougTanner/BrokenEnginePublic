#if defined(BT_CLIENT)

#include "Graphics/Managers/PipelineManager.h"

#include "Profile/ProfileManager.h"

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
			if (strcmp(rChunk.pHeader->pcPath, "Shaders\\Log.vert") == 0)
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
		mSpreadPipelines[iPass].Create(
		{
			.name = std::format("LightingSpread{}", iPass),
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
}

void PipelineManager::CreatePipelineShadows()
{
	mpPipelines[kPipelineShadowElevation].Create(
	{
		.name = "ShadowElevation",
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()},
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
		.flags = {kDepthTest, kDepthWrite, kCullBack, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersTerrainTerrainvertCrc), &mShaders.at(data::kShadersTerrainTerrainfragCrc)},
		.pVertexBuffer = &gpBufferManager->mTerrainMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainColorTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7Rock0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal1pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5SandNormal2pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandpngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal1jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal2jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC5RockNormal4jpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
		},
	});

	// Water
	mpPipelines[kPipelineWater].Create(
	{
		.name = "Water",
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersWaterWatervertCrc), &mShaders.at(data::kShadersWaterWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesCRyfjallet_PrefilteredR16G16B16A16_SFLOATCrc},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = TextureManager::kiWaterNormalCount, .ppTextures = mppWaterNormalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
		},
	});
}

void PipelineManager::CreateTerrainDataPipelines()
{
	struct TerrainDataPipelineDesc
	{
		Pipelines ePipeline;
		const char* pcName;
		common::crc_t fragmentShaderCrc;
		Texture& rTargetTexture;
		Texture** ppSourceTextures;
	};

	TerrainDataPipelineDesc pDescs[]
	{
		{.ePipeline = kPipelineTerrainElevation, .pcName = "TerrainElevation", .fragmentShaderCrc = data::kShadersTerrainTerrainElevationfragCrc, .rTargetTexture = gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture, .ppSourceTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()},
		{.ePipeline = kPipelineTerrainColor, .pcName = "TerrainColor", .fragmentShaderCrc = data::kShadersTerrainTerrainColorfragCrc, .rTargetTexture = gpTextureManager->mRenderTargetTextures.mTerrainColorTexture, .ppSourceTextures = gpTextureManager->mRenderTargetTextures.mColorTextures.data()},
		{.ePipeline = kPipelineTerrainNormal, .pcName = "TerrainNormal", .fragmentShaderCrc = data::kShadersTerrainTerrainNormalfragCrc, .rTargetTexture = gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture, .ppSourceTextures = gpTextureManager->mRenderTargetTextures.mNormalsTextures.data()},
		{.ePipeline = kPipelineTerrainAmbientOcclusion, .pcName = "TerrainAmbientOcclusion", .fragmentShaderCrc = data::kShadersTerrainTerrainAmbientOcclusionfragCrc, .rTargetTexture = gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture, .ppSourceTextures = gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data()},
	};

	for (const TerrainDataPipelineDesc& rDesc : pDescs)
	{
		mpPipelines[rDesc.ePipeline].Create(
		{
			.name = rDesc.pcName,
			.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
			.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(rDesc.fragmentShaderCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = rDesc.rTargetTexture.mVkRenderPass,
			.vkExtent3D = rDesc.rTargetTexture.mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = rDesc.ppSourceTextures},
			},
		});
	}
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
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
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
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
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

void PipelineManager::RecreatePipelineGroups(DestroyFlags_t flags)
{
	using enum DestroyFlags;

	// Stage 1: Lighting pipelines (must come before terrain/water and particles)
	if ((flags & kLightingTextures) || (flags & kTerrainElevation))
	{
		gpBufferManager->CreateLightingSpreadBuffers();
		CreateLightingPipelines();

		// Dynamic lighting pipelines render to lighting textures
		VkRenderPass vkLightingRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass;
		VkExtent3D vkLightingExtent = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineLighting, kDynamicPipelineAxisAlignedLighting, kDynamicPipelineHexShieldsLighting})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelines.mPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkLightingRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkLightingExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}

		// Debug texture pipeline references all lighting debug textures
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
	}

	// Stage 2: Shadow pipelines
	if ((flags & kShadowTextures) || (flags & kObjectShadows))
	{
		CreatePipelineShadows();
	}

	// Stage 3: Terrain and water (depend on lighting blur + shadow blur textures)
	if ((flags & kLightingTextures) || (flags & kShadowTextures) || (flags & kObjectShadows)
		|| (flags & kTerrainElevation) || (flags & kTerrainColor) || (flags & kTerrainNormal) || (flags & kTerrainAO)
		|| (flags & kSmokeTextures))
	{
		CreateLightingShadowDependentPipelines();
	}

	// Terrain data pipelines (render TO terrain detail textures)
	if ((flags & kTerrainElevation) || (flags & kTerrainColor) || (flags & kTerrainNormal) || (flags & kTerrainAO))
	{
		CreateTerrainDataPipelines();
	}

	// Smoke and wind pipelines
	if ((flags & kSmokeTextures) || (flags & kTerrainElevation))
	{
		CreateSmokeWindPipelines();
	}

	// Particle pipelines (sample smoke, wind, and terrain elevation textures;
	// spawn references update/render/lighting indirect buffers)
	if ((flags & kSmokeTextures) || (flags & kTerrainElevation) || (flags & kLightingTextures))
	{
		CreateParticlePipelines();
	}

	// Dynamic smoke/wind deposit pipelines render to smoke/wind textures
	if (flags & kSmokeTextures)
	{
		VkRenderPass vkSmokeRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkRenderPass;
		VkExtent3D vkSmokeExtent = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineSmokeAxisAligned, kDynamicPipelineSmoke})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelines.mPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkSmokeRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkSmokeExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}

		VkRenderPass vkWindOneRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mVkRenderPass;
		VkExtent3D vkWindOneExtent = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineWindDepositA, kDynamicPipelineWindDepositAxisAlignedA})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelines.mPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkWindOneRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkWindOneExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}

		VkRenderPass vkWindTwoRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mVkRenderPass;
		VkExtent3D vkWindTwoExtent = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineWindDepositB, kDynamicPipelineWindDepositAxisAlignedB})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelines.mPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkWindTwoRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkWindTwoExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}
	}

	// Dynamic visible lights sample terrain elevation texture (no render target change)
	if (flags & kTerrainElevation)
	{
		for (auto& [rCrc, rpPipeline] : mDynamicPipelines.mPipelineMaps[kDynamicPipelineVisibleLights])
		{
			rpPipeline->Create(rpPipeline->mInfo);
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
