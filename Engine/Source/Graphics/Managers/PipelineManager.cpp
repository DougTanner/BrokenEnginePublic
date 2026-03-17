#if defined(BT_CLIENT)

#include "Graphics/Managers/PipelineManager.h"

#include "Profile/ProfileManager.h"

#include "Data/Model.h"
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

		if constexpr (!kbEnableDebugPrintf)
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

	CreateLightingPipelines();
	CreatePipelineShadows();
	CreateLightingShadowDependantPipelines();

	if constexpr (kbEnableDebugPrintf)
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

	CreateSmokeWindPipelines();

	CreateParticlePipelines();

	game::FrameInterpolate::GraphicsResources();
}

PipelineManager::~PipelineManager()
{
	gpPipelineManager = nullptr;
}

void PipelineManager::CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount])
{
	for (int64_t i = 0; i < gpTextureManager->mRenderTargetTextures.miLightingBlurCount; ++i)
	{
		pLightingBlurPipelines[i].Create(
		{
			.name = "LightingBlur",
			.flags = {kRenderTarget, kPushConstants},
			.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersLightingLightingBlurfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = pLightingBlurTextures[i].mVkRenderPass,
			.vkExtent3D = pLightingBlurTextures[i].mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = i == 0 ? pLightingTexture : &pLightingBlurTextures[i - 1]},
			},
		});
	}

	// Select which blur textures to combine based on current settings
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	Texture* ppLightingBlurTextures[shaders::kiMaxLightingBlurCount] {};
	for (int64_t i = 0; i < iBlurTextureCount; ++i)
	{
		ppLightingBlurTextures[i] = &pLightingBlurTextures[iCombineTextureIndex + 1 + i];
	}
	for (int64_t i = iBlurTextureCount; i < shaders::kiMaxLightingBlurCount; ++i)
	{
		// Not used, just placeholders
		ppLightingBlurTextures[i] = &pLightingBlurTextures[i];
	}
	mpPipelines[eCombinePipeline].Create(
	{
		.name = "LightingCombine",
		.flags = {kRenderTarget, kAdd},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersLightingLightingCombinefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = pLightingBlurTextures[iCombineTextureIndex].mVkRenderPass,
		.vkExtent3D = pLightingBlurTextures[iCombineTextureIndex].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = std::size(ppLightingBlurTextures), .ppTextures = ppLightingBlurTextures},
		},
	});
}

void PipelineManager::CreateLightingPipelines()
{
	mpPipelines[kPipelineLongParticlesLighting].Create(
	{
		.name = "LightingParticlesLong",
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersParticlesLightingParticlesRendervertCrc), &mShaders.at(data::kShadersParticlesLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelineSquareParticlesLighting].Create(
	{
		.name = "LightingParticlesSquare",
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersParticlesLightingParticlesRendervertCrc), &mShaders.at(data::kShadersParticlesLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	CreateLightingBlurCombinePipelines(kPipelineRedLightingCombine, &gpTextureManager->mRenderTargetTextures.mpLightingTextures[0], mpRedLightingBlurPipelines, gpTextureManager->mRenderTargetTextures.mpRedLightingBlurTextures);
	CreateLightingBlurCombinePipelines(kPipelineGreenLightingCombine, &gpTextureManager->mRenderTargetTextures.mpLightingTextures[1], mpGreenLightingBlurPipelines, gpTextureManager->mRenderTargetTextures.mpGreenLightingBlurTextures);
	CreateLightingBlurCombinePipelines(kPipelineBlueLightingCombine, &gpTextureManager->mRenderTargetTextures.mpLightingTextures[2], mpBlueLightingBlurPipelines, gpTextureManager->mRenderTargetTextures.mpBlueLightingBlurTextures);
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

	mpPipelines[kPipelineShadowBlurH].Create(
	{
		.name = "ShadowBlurH",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersShadowShadowBlurHcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture},
		},
	});

	mpPipelines[kPipelineShadowBlurV].Create(
	{
		.name = "ShadowBlurV",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersShadowShadowBlurVcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurIntermediateTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
		},
	});

	mpPipelines[kPipelineObjectShadowsBlur].Create(
	{
		.name = "ShadowBlur",
		.flags = {kRenderTarget},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersShadowObjectShadowsBlurfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture},
		},
	},
	false);
}

void PipelineManager::CreateLightingShadowDependantPipelines()
{
	// Terrain
	mpPipelines[kPipelineTerrain].Create(
	{
		.name = "Terrain",
		.flags = {kDepthTest, kDepthWrite, kCullBack, kSampleShading, kUpdateAfterBind},
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
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandNormal0jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandNormal1pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandNormal2pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7SandpngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7RockNormal1jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7RockNormal2jpgCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesTerrainBC7RockNormal4jpgCrc},
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
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc}, // 4 8
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC70pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC73jpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
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
		{kPipelineTerrainElevation, "TerrainElevation", data::kShadersTerrainTerrainElevationfragCrc, gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture, gpTextureManager->mRenderTargetTextures.mElevationTextures.data()},
		{kPipelineTerrainColor, "TerrainColor", data::kShadersTerrainTerrainColorfragCrc, gpTextureManager->mRenderTargetTextures.mTerrainColorTexture, gpTextureManager->mRenderTargetTextures.mColorTextures.data()},
		{kPipelineTerrainNormal, "TerrainNormal", data::kShadersTerrainTerrainNormalfragCrc, gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture, gpTextureManager->mRenderTargetTextures.mNormalsTextures.data()},
		{kPipelineTerrainAmbientOcclusion, "TerrainAmbientOcclusion", data::kShadersTerrainTerrainAmbientOcclusionfragCrc, gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture, gpTextureManager->mRenderTargetTextures.mAmbientOcclusionTextures.data()},
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

	mpPipelines[kPipelineSmokeSpreadB].Create(
	{
		.name = "SmokeSpreadB",
		.flags = {kRenderTarget, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersSmokeSmokeSpreadTwofragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
		},
	});

	mpPipelines[kPipelineSmokeSpreadA].Create(
	{
		.name = "SmokeSpreadA",
		.flags = {kRenderTarget, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &mShaders.at(data::kShadersSmokeSmokeSpreadOnefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokeSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
		},
	});

	mpPipelines[kPipelineWindClearA].Create(
	{
		.name = "WindClearA",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
	mpPipelines[kPipelineWindSpreadA].Create(
	{
		.name = "WindSpreadA",
		.flags = {kRenderTarget, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &mShaders.at(data::kShadersWindWindSpreadfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mWindSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
		},
	});
	mpPipelines[kPipelineWindClearB].Create(
	{
		.name = "WindClearB",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
	mpPipelines[kPipelineWindSpreadB].Create(
	{
		.name = "WindSpreadB",
		.flags = {kRenderTarget, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &mShaders.at(data::kShadersWindWindSpreadfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mWindSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
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
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
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
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineLongParticlesLighting].mIndirectVkBuffer},
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
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
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
			{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[kPipelineSquareParticlesLighting].mIndirectVkBuffer},
		},
	});
}

void PipelineManager::RecreatePipelineGroups(DestroyFlags_t flags)
{
	using enum DestroyFlags;

	// Stage 1: Lighting pipelines (must come before terrain/water and particles)
	if (flags & kLightingTextures)
	{
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
		CreateLightingShadowDependantPipelines();
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
