#include "Graphics/Managers/PipelineManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

PipelineManager::PipelineManager()
{
	gpPipelineManager = this;

	SCOPED_BOOT_TIMER(kBootTimerPipelineManager);

	CreateLightingPipelines();
	CreateShadowPipelines();
	CreateLightingShadowDependantPipelines();

#if defined(ENABLE_DEBUG_PRINTF_EXT)
	mpPipelines[kPipelineLog].Create(
	{
		.pcName = "Log",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersLogvertCrc), &gpShaderManager->mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLogTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mLogTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
#endif

	mpPipelines[kPipelineHexShields].Create(
	{
		.pcName = "HexShield",
		.flags = {kIndirectHostVisible, kPushConstants, kAlphaBlend, kDepthTest, kCullBack},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersHexShieldvertCrc), &gpShaderManager->mShaders.at(data::kShadersHexShieldfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronFNDualGeodesicIcosahedronobjCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mHexShieldsStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesCSkyboxCrc)},
		},
	});

	mpPipelines[kPipelineTerrainElevation].Create(
	{
		.pcName = "TerrainElevation",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainColor].Create(
	{
		.pcName = "TerrainColor",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainColorfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainColorTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainColorTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mColorTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainNormal].Create(
	{
		.pcName = "TerrainNormal",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainNormalfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainNormalTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainNormalTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mNormalsTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainAmbientOcclusion].Create(
	{
		.pcName = "TerrainAmbientOcclusion",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainAmbientOcclusionfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainAmbientOcclusionTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainAmbientOcclusionTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mAmbientOcclusionTextures.data()},
		},
	});

	mpPipelines[kPipelineProfileText].Create(
	{
		.pcName = "ProfileText",
		.flags = {kIndirectHostVisible, kAlphaBlend, kNoWireframe},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedvertCrc), &gpShaderManager->mShaders.at(data::kShadersProfileTextfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mTextStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesUiBC4NotoSansRegularpngCrc)},
		},
	});

	mpPipelines[kPipelineWidgets].Create(
	{
		.pcName = "Widgets",
		.flags = {kIndirectHostVisible, kAlphaBlend, kNoWireframe},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersWidgetsvertCrc), &gpShaderManager->mShaders.at(data::kShadersWidgetsfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mWidgetsStorageBuffers.data()},
			{.flags = kSamplerClamp},
			{.flags = kUiTextures},
		},
	});

	mpPipelines[kPipelineSmokeClearOne].Create(
	{
		.pcName = "SmokeClearOne",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
	mpPipelines[kPipelineSmokeClearTwo].Create(
	{
		.pcName = "SmokeClearTwo",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});
	
	Texture* pSmokeTextures[]
	{
		&gpTextureManager->mSmokeGradientTexture,
		&gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC40jpgCrc),
		&gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC41jpgCrc),
		&gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC42jpgCrc),
		&gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC43jpgCrc),
		&gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC44jpgCrc),
	};
	mpPipelines[kPipelineSmokePuffs].Create(
	{
		.pcName = "SmokePuffs",
		.flags = {kRenderTarget, kPushConstants, kAdd, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokePuffsStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = std::size(pSmokeTextures), .ppTextures = pSmokeTextures},
		},
	});

	mpPipelines[kPipelineSmokeTrails].Create(
	{
		.pcName = "SmokeTrails",
		.flags = {kRenderTarget, kPushConstants, kAdd, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokeTrailsStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = std::size(pSmokeTextures), .ppTextures = pSmokeTextures},
		},
	});

	mpPipelines[kPipelineSmokeSpreadTwo].Create(
	{
		.pcName = "SmokeSpreadTwo",
		.flags = {kRenderTarget, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSpreadTwofragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc)},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineSmokeSpreadOne].Create(
	{
		.pcName = "SmokeSpreadOne",
		.flags = {kRenderTarget, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedvertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSpreadOnefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokeSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc)},
		},
	});

	// Long particles
	mpPipelines[kPipelineLongParticlesUpdate].Create(
	{
		.pcName = "LongParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineLongParticlesRender].Create(
	{
		.pcName = "LongParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersLongParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiParticlesCookieCount, .ppTextures = gpTextureManager->mpLongParticleTextures},
			// {.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
		},
	});

	mpPipelines[kPipelineLongParticlesSpawn].Create(
	{
		.pcName = "LongParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesSpawncompCrc)},
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

	// Square particles
	mpPipelines[kPipelineSquareParticlesUpdate].Create(
	{
		.pcName = "SquareParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineSquareParticlesRender].Create(
	{
		.pcName = "SquareParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersSquareParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = kSquareParticleCrcs.miCount, .ppTextures = gpTextureManager->mpSquareParticleTextures},
			// {.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
		},
	});

	mpPipelines[kPipelineSquareParticlesSpawn].Create(
	{
		.pcName = "SquareParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesSpawncompCrc)},
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

	mpPipelines[kPipelineBillboards].Create(
	{
		.pcName = "Billboards",
		.flags = {kIndirectHostVisible, kSampleShading, kAlphaBlend},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersBillboardsvertCrc), &gpShaderManager->mShaders.at(data::kShadersBillboardsfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mBillboardsStorageBuffers.data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	mGltfPipelines.CreateGltfShadowPipelines();
	mGltfPipelines.CreateGltfPipelines();
}

PipelineManager::~PipelineManager()
{
	gpPipelineManager = nullptr;
}

void PipelineManager::CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount])
{
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
	{
		pLightingBlurPipelines[i].Create(
		{
			.pcName = "LightingBlur",
			.flags = {kRenderTarget, kPushConstants},
			.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingBlurfragCrc)},
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
		.pcName = "LightingCombine",
		.flags = {kRenderTarget, kAdd},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingCombinefragCrc)},
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
	mpPipelines[kPipelineVisibleLights].Create(
	{
		.pcName = "VisibleLights",
		.flags = {kIndirectHostVisible, kAdd, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVisibleLightvertCrc), &gpShaderManager->mShaders.at(data::kShadersVisibleLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mVisibleLightsStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelineAreaLights].Create(
	{
		.pcName = "AreaLights",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mpLightingTextures[0].mVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mAreaLightsStorageBuffers.data()},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelinePointLights].Create(
	{
		.pcName = "PointLights",
		.flags = {kRenderTarget, kPushConstants, kMax, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersPointLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mpLightingTextures[0].mVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mPointLightsStorageBuffers.data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	mpPipelines[kPipelineHexShieldsLighting].Create(
	{
		.pcName = "HexShieldLighting",
		.flags = {kRenderTarget, kPushConstants, kMax, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersHexShieldvertCrc), &gpShaderManager->mShaders.at(data::kShadersHexShieldLightingfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronFNDualGeodesicIcosahedronobjCrc),
		.vkRenderPass = gpTextureManager->mpLightingTextures[0].mVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mHexShieldsStorageBuffers.data()},
		},
	});

	mpPipelines[kPipelineLongParticlesLighting].Create(
	{
		.pcName = "LightingParticlesLong",
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersLightingParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mpLightingTextures[0].mVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiParticlesCookieCount, .ppTextures = gpTextureManager->mpLongParticleTextures},
		},
	});

	mpPipelines[kPipelineSquareParticlesLighting].Create(
	{
		.pcName = "LightingParticlesSquare",
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersLightingParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mpLightingTextures[0].mVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiParticlesCookieCount, .ppTextures = gpTextureManager->mpSquareParticleTextures},
		},
	});

	CreateLightingBlurCombinePipelines(kPipelineRedLightingCombine, &gpTextureManager->mpLightingTextures[0], mpRedLightingBlurPipelines, gpTextureManager->mpRedLightingBlurTextures);
	CreateLightingBlurCombinePipelines(kPipelineGreenLightingCombine, &gpTextureManager->mpLightingTextures[1], mpGreenLightingBlurPipelines, gpTextureManager->mpGreenLightingBlurTextures);
	CreateLightingBlurCombinePipelines(kPipelineBlueLightingCombine, &gpTextureManager->mpLightingTextures[2], mpBlueLightingBlurPipelines, gpTextureManager->mpBlueLightingBlurTextures);
}

void PipelineManager::CreateShadowPipelines()
{
	mpPipelines[kPipelineShadowElevation].Create(
	{
		.pcName = "ShadowElevation",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = gpIslands->miCount, .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	});

	mpPipelines[kPipelineShadow].Create(
	{
		.pcName = "Shadow",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersShadowcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowElevationTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mShadowTexture},
		},
	});

	mpPipelines[kPipelineShadowBlur].Create(
	{
		.pcName = "ShadowBlur",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersShadowBlurcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mShadowBlurTexture},
		},
	});

	mpPipelines[kPipelineObjectShadowsBlur].Create(
	{
		.pcName = "ShadowBlur",
		.flags = {kRenderTarget},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersObjectShadowsBlurfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mObjectShadowsBlurTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mObjectShadowsBlurTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mObjectShadowsTexture},
		},
	},
	false);
}

void PipelineManager::CreateLightingShadowDependantPipelines()
{
	// Terrain
	mpPipelines[kPipelineTerrain].Create(
	{
		.pcName = "Terrain",
		.flags = {kDepthTest, kDepthWrite, kCullBack, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersTerrainvertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainfragCrc)},
		.pVertexBuffer = &gpBufferManager->mTerrainMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mppLightingFinalTextures)), .ppTextures = gpTextureManager->mppLightingFinalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainColorTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainNormalTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainAmbientOcclusionTexture},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7Rock0jpgCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7SandNormal0jpgCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7SandNormal1pngCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7SandNormal2pngCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7SandpngCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7RockNormal1jpgCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7RockNormal2jpgCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesTerrainBC7RockNormal4jpgCrc)},
		},
	});

	// Water
	mpPipelines[kPipelineWater].Create(
	{
		.pcName = "Water",
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersWatervertCrc), &gpShaderManager->mShaders.at(data::kShadersWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mppLightingFinalTextures)), .ppTextures = gpTextureManager->mppLightingFinalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mGltfPreFilteredTexture},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesWaterBC4NoisepngCrc)}, // 4 8
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesWaterBC70pngCrc)},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesWaterBC73jpgCrc)},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesWaterDepthLutpngCrc)},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
		},
	});
}

} // namespace engine
