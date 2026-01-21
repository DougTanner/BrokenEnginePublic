#include "Graphics/Managers/PipelineManager.h"

#include "File/FileManager.h"
#include "Frame/Frame.h"
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
	CreatePipelineShadows();
	CreateLightingShadowDependantPipelines();

#if defined(ENABLE_DEBUG_PRINTF_EXT)
	mpPipelines[kPipelineLog].Create(
	{
		.name = "Log",
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

	mpPipelines[kPipelineTerrainElevation].Create(
	{
		.name = "TerrainElevation",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(gpIslands->mIslands.size()), .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainColor].Create(
	{
		.name = "TerrainColor",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainColorfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainColorTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainColorTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(gpIslands->mIslands.size()), .ppTextures = gpTextureManager->mColorTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainNormal].Create(
	{
		.name = "TerrainNormal",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainNormalfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainNormalTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainNormalTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(gpIslands->mIslands.size()), .ppTextures = gpTextureManager->mNormalsTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainAmbientOcclusion].Create(
	{
		.name = "TerrainAmbientOcclusion",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainAmbientOcclusionfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainAmbientOcclusionTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainAmbientOcclusionTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(gpIslands->mIslands.size()), .ppTextures = gpTextureManager->mAmbientOcclusionTextures.data()},
		},
	});

	mpPipelines[kPipelineProfileText].Create(
	{
		.name = "ProfileText",
		.flags = {kIndirectHostVisible, kAlphaBlend, kNoWireframe},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &gpShaderManager->mShaders.at(data::kShadersUiProfileTextfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mTextStorageBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesUiBC4NotoSansRegularpngCrc},
		},
	});

	mpPipelines[kPipelineWidgets].Create(
	{
		.name = "Widgets",
		.flags = {kIndirectHostVisible, kAlphaBlend, kNoWireframe},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersUiWidgetsvertCrc), &gpShaderManager->mShaders.at(data::kShadersUiWidgetsfragCrc)},
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
		.name = "SmokeClearOne",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersClearfragCrc)},
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
		.name = "SmokeClearTwo",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
		},
	});

	mpPipelines[kPipelineSmokeSpreadTwo].Create(
	{
		.name = "SmokeSpreadTwo",
		.flags = {kRenderTarget, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSmokeSpreadTwofragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineSmokeSpreadOne].Create(
	{
		.name = "SmokeSpreadOne",
		.flags = {kRenderTarget, kIndirectHostVisible},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSmokeSpreadOnefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokeSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
		},
	});

	// Long particles
	mpPipelines[kPipelineLongParticlesUpdate].Create(
	{
		.name = "LongParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mLongParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineLongParticlesRender].Create(
	{
		.name = "LongParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesLongParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesParticlesRenderfragCrc)},
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
		.name = "LongParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesParticlesSpawncompCrc)},
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
		.name = "SquareParticlesUpdate",
		.flags = {kCompute, kIndirectDeviceLocal},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesParticlesUpdatecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	mpPipelines[kPipelineSquareParticlesRender].Create(
	{
		.name = "SquareParticlesRender",
		.flags = {kIndirectDeviceLocal, kDepthTest, kAdd},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesSquareParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesParticlesRenderfragCrc)},
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
		.name = "SquareParticlesSpawn",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesParticlesSpawncompCrc)},
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

	game::FrameInterpolate::GraphicsResources();
}

PipelineManager::~PipelineManager()
{
	gpPipelineManager = nullptr;
}

GltfPipeline* PipelineManager::CreateGltfPipeline(const GltfPipelineSpec& spec)
{
	std::unique_ptr<GltfPipeline> pGltfPipeline = std::make_unique<GltfPipeline>();
	pGltfPipeline->Create(spec.gltfCrc, spec.pipelineInfo, spec.bAddGltfDescriptors);

	GltfPipeline* pResult = pGltfPipeline.get();
	mDynamicGltfPipelines.push_back(std::move(pGltfPipeline));

	return pResult;
}

void PipelineManager::CreateDynamicGltfPipeline(common::crc_t crc, const char* name, common::crc_t gltfCrc, common::crc_t modelVertexBufferCrc, Buffer* pStorageBuffers)
{
	// Skip if pipeline already exists
	if (mDynamicGltfPipelineMap.contains(crc))
	{
		return;
	}

	GltfPipeline* pPipeline = CreateGltfPipeline(
	{
		.name = name,
		.gltfCrc = gltfCrc,
		.pipelineInfo =
		{
			.name = name,
			.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading, kUpdateAfterBind},
			.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersGltfGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersGltfGltffragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelVertexBufferCrc),
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bAddGltfDescriptors = true,
		.bIsPipelineShadow = false,
	});

	mDynamicGltfPipelineMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicGltfPipelineShadow(common::crc_t crc, const char* name, common::crc_t gltfCrc, common::crc_t modelVertexBufferCrc, Buffer* pStorageBuffers)
{
	// Skip if shadow pipeline already exists
	if (mDynamicGltfPipelineShadowMap.contains(crc))
	{
		return;
	}

	// Create shadow variant of pipeline name
	std::string shadowName = std::string(name) + "Shadow";

	// Create shadow pipeline with minimal descriptor sets
	GltfPipeline* pPipelineShadow = CreateGltfPipeline(
	{
		.name = shadowName.c_str(),
		.gltfCrc = gltfCrc,
		.pipelineInfo =
		{
			.name = shadowName.c_str(),
			.flags = {kRenderTarget, kIndirectHostVisible, kPushConstants, kUpdateAfterBind},
			.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersGltfGltfvertCrc), &gpShaderManager->mShaders.at(data::kShadersGltfGltfShadowfragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelVertexBufferCrc),
			.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
			.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bAddGltfDescriptors = false,
		.bIsPipelineShadow = true,
	});

	mDynamicGltfPipelineShadowMap[crc] = pPipelineShadow;
}

void PipelineManager::CreateDynamicPipelineLighting(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if lighting pipeline already exists
	if (mDynamicPipelinesLightingMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for area light rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants, PipelineFlags::kIndirectHostVisible, PipelineFlags::kMax, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = DescriptorFlags::kSamplerRepeat},
			{.flags = DescriptorFlags::kTextures},
		},
	});

	// Register pipeline in lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesLightingMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineVisibleLights(common::crc_t crc, const char* name, Buffer* pStorageBuffers)
{
	// Skip if visible lights pipeline already exists
	if (mDynamicPipelinesVisibleLightsMap.contains(crc))
	{
		return;
	}

	// Allocate pipeline for visible lights rendering in main pass
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kIndirectHostVisible, PipelineFlags::kAdd, PipelineFlags::kSampleShading, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersLightingVisibleLightvertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingVisibleLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			{.flags = DescriptorFlags::kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = DescriptorFlags::kSamplerRepeat},
			{.flags = DescriptorFlags::kTextures},
		},
	});

	// Register pipeline in visible lights map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesVisibleLightsMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineAxisAlignedLighting(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if axis-aligned lighting pipeline already exists
	if (mDynamicPipelinesAxisAlignedLightingMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for point light rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants, PipelineFlags::kIndirectHostVisible, PipelineFlags::kMax, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingPointLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = DescriptorFlags::kSamplerClamp},
			{.flags = DescriptorFlags::kTextures},
		},
	});

	// Register pipeline in axis-aligned lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesAxisAlignedLightingMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineBillboards(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if billboards pipeline already exists
	if (mDynamicPipelinesBillboardsMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this billboards pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for billboard rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kIndirectHostVisible, PipelineFlags::kSampleShading, PipelineFlags::kAlphaBlend, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesBillboardsvertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesBillboardsfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = DescriptorFlags::kSamplerClamp},
			{.flags = DescriptorFlags::kTextures},
		},
	});

	// Register pipeline in billboards map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesBillboardsMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineSmokeAxisAligned(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if smoke axis-aligned pipeline already exists
	if (mDynamicPipelinesSmokeAxisAlignedMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for smoke puff rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants, PipelineFlags::kIndirectHostVisible, PipelineFlags::kAdd, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = {DescriptorFlags::kCombinedSamplers, DescriptorFlags::kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC44jpgCrc},
		},
	});

	// Register pipeline in smoke axis-aligned map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesSmokeAxisAlignedMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineSmoke(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if smoke pipeline already exists
	if (mDynamicPipelinesSmokeMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for smoke trail rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants, PipelineFlags::kIndirectHostVisible, PipelineFlags::kAdd, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = {DescriptorFlags::kCombinedSamplers, DescriptorFlags::kSamplerClamp}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeGradientTexture},
		},
	});

	// Register pipeline in smoke map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesSmokeMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineHexShields(common::crc_t crc, const char* name, int64_t iBufferSize)
{
	// Skip if HexShields pipeline already exists
	if (mDynamicPipelinesHexShieldsMap.contains(crc))
	{
		return;
	}

	// Create storage buffer for this HexShields pipeline
	gpBufferManager->CreateDynamicBuffer(crc, name, iBufferSize);

	// Allocate pipeline and configure for HexShields rendering (uses DualGeodesicIcosahedron mesh)
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kIndirectHostVisible, PipelineFlags::kPushConstants, PipelineFlags::kAlphaBlend, PipelineFlags::kDepthTest, PipelineFlags::kCullBack, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersObjectsHexShieldvertCrc), &gpShaderManager->mShaders.at(data::kShadersObjectsHexShieldfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronFNDualGeodesicIcosahedronobjCrc),
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
			{.flags = DescriptorFlags::kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesCSkyboxCrc},
		},
	});

	// Register pipeline in HexShields map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesHexShieldsMap[crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineHexShieldsLighting(common::crc_t crc, const char* name)
{
	// Skip if HexShields lighting pipeline already exists
	if (mDynamicPipelinesHexShieldsLightingMap.contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for HexShields lighting pass (shares buffer with main HexShields pipeline)
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants, PipelineFlags::kMax, PipelineFlags::kIndirectHostVisible, PipelineFlags::kUpdateAfterBind},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersObjectsHexShieldvertCrc), &gpShaderManager->mShaders.at(data::kShadersObjectsHexShieldLightingfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronFNDualGeodesicIcosahedronobjCrc),
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = DescriptorFlags::kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers.at(crc).data()},
		},
	});

	// Register pipeline in HexShields lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelinesHexShieldsLightingMap[crc] = pPipeline;
}

void PipelineManager::CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount])
{
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
	{
		pLightingBlurPipelines[i].Create(
		{
			.name = "LightingBlur",
			.flags = {kRenderTarget, kPushConstants},
			.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingLightingBlurfragCrc)},
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
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersLightingLightingCombinefragCrc)},
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
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesLightingParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
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
		.name = "LightingParticlesSquare",
		.flags = {kRenderTarget, kPushConstants, kIndirectDeviceLocal, kMax},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersParticlesLightingParticlesRendervertCrc), &gpShaderManager->mShaders.at(data::kShadersParticlesLightingParticlesRenderfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
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

void PipelineManager::CreatePipelineShadows()
{
	mpPipelines[kPipelineShadowElevation].Create(
	{
		.name = "ShadowElevation",
		.flags = {kRenderTarget, kPushConstants},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(gpIslands->mIslands.size()), .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	});

	mpPipelines[kPipelineShadow].Create(
	{
		.name = "Shadow",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersShadowShadowcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowElevationTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mShadowTexture},
		},
	});

	mpPipelines[kPipelineShadowBlur].Create(
	{
		.name = "ShadowBlur",
		.flags = {kCompute},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersShadowShadowBlurcompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mShadowBlurTexture},
		},
	});

	mpPipelines[kPipelineObjectShadowsBlur].Create(
	{
		.name = "ShadowBlur",
		.flags = {kRenderTarget},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &gpShaderManager->mShaders.at(data::kShadersShadowObjectShadowsBlurfragCrc)},
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
		.name = "Terrain",
		.flags = {kDepthTest, kDepthWrite, kCullBack, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersTerrainTerrainvertCrc), &gpShaderManager->mShaders.at(data::kShadersTerrainTerrainfragCrc)},
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
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kSampleShading},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersWaterWatervertCrc), &gpShaderManager->mShaders.at(data::kShadersWaterWaterfragCrc)},
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
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc}, // 4 8
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC70pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC73jpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
		},
	});
}

} // namespace engine
