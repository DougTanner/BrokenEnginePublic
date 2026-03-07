#ifdef BT_CLIENT

#include "Graphics/Managers/DynamicPipelines.h"

#include "Data/Model.h"
#include "Data/Shader.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

DynamicPipelines::DynamicPipelines(std::unordered_map<common::crc_t, Shader>& rShaders)
: mrShaders(rShaders)
{
}

ModelPipeline* DynamicPipelines::CreateModelPipeline(const ModelPipelineSpec& rModelPipelineSpec)
{
	std::unique_ptr<ModelPipeline> pModelPipeline = std::make_unique<ModelPipeline>();
	pModelPipeline->Create(rModelPipelineSpec.sceneCrc, rModelPipelineSpec.pipelineInfo, rModelPipelineSpec.bAddModelDescriptors, rModelPipelineSpec.bIsPipelineShadow);

	ModelPipeline* pResult = pModelPipeline.get();
	mModelPipelines.push_back(std::move(pModelPipeline));

	return pResult;
}

void DynamicPipelines::CreateModelPipeline(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers)
{
	// Skip if pipeline already exists
	if (mModelPipelineMaps[kDynamicModelPipelineModel].contains(crc))
	{
		return;
	}

	// Look up the model CRC and animation flag from the glTF header
	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	common::crc_t modelCrc = rChunk.pHeader->sceneHeader.modelCrc;
	bool bHasAnimation = rChunk.pHeader->sceneHeader.bHasAnimation;

	// Select vertex shader based on animation flag
	common::crc_t vertexShaderCrc = bHasAnimation ? data::kShadersModelModelSkinnedvertCrc : data::kShadersModelModelStaticvertCrc;

	ModelPipeline* pPipeline = CreateModelPipeline(
	{
		.name = name,
		.sceneCrc = sceneCrc,
		.pipelineInfo =
		{
			.name = name,
			.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading, kUpdateAfterBind, kMultiSet},
			.ppShaders = {&mrShaders.at(vertexShaderCrc), &mrShaders.at(data::kShadersModelModelfragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelCrc),
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bAddModelDescriptors = true,
		.bIsPipelineShadow = false,
	});

	mModelPipelineMaps[kDynamicModelPipelineModel][crc] = pPipeline;
}

void DynamicPipelines::CreateModelPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers)
{
	// Skip if shadow pipeline already exists
	if (mModelPipelineMaps[kDynamicModelPipelineModelShadow].contains(crc))
	{
		return;
	}

	// Look up the model CRC and animation flag from the glTF header
	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	common::crc_t modelCrc = rChunk.pHeader->sceneHeader.modelCrc;
	bool bHasAnimation = rChunk.pHeader->sceneHeader.bHasAnimation;

	// Select vertex shader based on animation flag
	common::crc_t vertexShaderCrc = bHasAnimation ? data::kShadersModelModelSkinnedvertCrc : data::kShadersModelModelStaticvertCrc;

	// Create shadow variant of pipeline name (stored in map to outlive this function)
	std::string& rShadowName = mShadowPipelineNames[crc] = std::string(name) + "Shadow";

	// Create shadow pipeline with minimal descriptor sets
	ModelPipeline* pPipelineShadow = CreateModelPipeline(
	{
		.name = rShadowName,
		.sceneCrc = sceneCrc,
		.pipelineInfo =
		{
			.name = rShadowName,
			.flags = {kRenderTarget, kIndirectHostVisible, kPushConstants, kUpdateAfterBind},
			.ppShaders = {&mrShaders.at(vertexShaderCrc), &mrShaders.at(data::kShadersModelModelShadowfragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelCrc),
			.vkRenderPass = gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mVkRenderPass,
			.vkExtent3D = gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bAddModelDescriptors = true,
		.bIsPipelineShadow = true,
	});

	mModelPipelineMaps[kDynamicModelPipelineModelShadow][crc] = pPipelineShadow;
}

void DynamicPipelines::CreatePipelineLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if lighting pipeline already exists
	if (mPipelineMaps[kDynamicPipelineLighting].contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for area light rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mrShaders.at(data::kShadersLightingAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
		},
	});

	// Register pipeline in lighting map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineLighting][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers)
{
	// Skip if visible lights pipeline already exists
	if (mPipelineMaps[kDynamicPipelineVisibleLights].contains(crc))
	{
		return;
	}

	// Allocate pipeline for visible lights rendering in main pass
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kAddAlpha, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersLightingVisibleLightvertCrc), &mrShaders.at(data::kShadersLightingVisibleLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
		},
	});

	// Register pipeline in visible lights map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineVisibleLights][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineAxisAlignedLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if axis-aligned lighting pipeline already exists
	if (mPipelineMaps[kDynamicPipelineAxisAlignedLighting].contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for point light rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersLightingPointLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	// Register pipeline in axis-aligned lighting map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineAxisAlignedLighting][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineBillboards(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if billboards pipeline already exists
	if (mPipelineMaps[kDynamicPipelineBillboards].contains(crc))
	{
		return;
	}

	// Create storage buffer for this billboards pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for billboard rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kSampleShading, kAlphaBlend, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersParticlesBillboardsvertCrc), &mrShaders.at(data::kShadersParticlesBillboardsfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	// Register pipeline in billboards map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineBillboards][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if smoke axis-aligned pipeline already exists
	if (mPipelineMaps[kDynamicPipelineSmokeAxisAligned].contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for smoke puff rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC44jpgCrc},
		},
	});

	// Register pipeline in smoke axis-aligned map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineSmokeAxisAligned][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if smoke pipeline already exists
	if (mPipelineMaps[kDynamicPipelineSmoke].contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for smoke trail rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mrShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeGradientTexture},
		},
	});

	// Register pipeline in smoke map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineSmoke][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineWindDepositA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if wind deposit pipeline already exists
	if (mPipelineMaps[kDynamicPipelineWindDepositA].contains(crc))
	{
		return;
	}

	// Create storage buffer for this wind deposit pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for wind deposit rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mrShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		},
	});

	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineWindDepositA][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineWindDepositB(common::crc_t crc, std::string_view name)
{
	// Skip if wind deposit B pipeline already exists
	if (mPipelineMaps[kDynamicPipelineWindDepositB].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for wind deposit rendering (texture B target)
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mrShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		},
	});

	Pipeline* pPipelineTwo = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineWindDepositB][crc] = pPipelineTwo;
}

void DynamicPipelines::CreatePipelineWindDepositAxisAlignedA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if axis-aligned wind deposit A pipeline already exists
	if (mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].contains(crc))
	{
		return;
	}

	// Create storage buffer for this wind deposit pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for axis-aligned wind deposit rendering
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesParticlesBC4Square16pngCrc},
		},
	});

	// Register pipeline in axis-aligned wind deposit A map
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineWindDepositAxisAlignedB(common::crc_t crc, std::string_view name)
{
	// Skip if axis-aligned wind deposit B pipeline already exists
	if (mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for axis-aligned wind deposit rendering (texture B target)
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesParticlesBC4Square16pngCrc},
		},
	});

	// Register pipeline in axis-aligned wind deposit B map
	Pipeline* pPipelineAxisAlignedTwo = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB][crc] = pPipelineAxisAlignedTwo;
}

void DynamicPipelines::CreatePipelineHexShields(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if HexShields pipeline already exists
	if (mPipelineMaps[kDynamicPipelineHexShields].contains(crc))
	{
		return;
	}

	// Create storage buffer for this HexShields pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for HexShields rendering (uses DualGeodesicIcosahedron mesh)
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kPushConstants, kAlphaBlend, kDepthTest, kCullBack, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersObjectsHexShieldvertCrc), &mrShaders.at(data::kShadersObjectsHexShieldfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronDualGeodesicIcosahedrongltfMODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesCSkyboxCrc},
		},
	});

	// Register pipeline in HexShields map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineHexShields][crc] = pPipeline;
}

void DynamicPipelines::CreatePipelineHexShieldsLighting(common::crc_t crc, std::string_view name)
{
	// Skip if HexShields lighting pipeline already exists
	if (mPipelineMaps[kDynamicPipelineHexShieldsLighting].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for HexShields lighting pass (shares buffer with main HexShields pipeline)
	size_t iPipelineIndex = mPipelines.size();
	mPipelines.push_back(std::make_unique<Pipeline>());
	mPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kMax, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersObjectsHexShieldvertCrc), &mrShaders.at(data::kShadersObjectsHexShieldLightingfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronDualGeodesicIcosahedrongltfMODELCrc),
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
		},
	});

	// Register pipeline in HexShields lighting map for iteration during rendering
	Pipeline* pPipeline = mPipelines[iPipelineIndex].get();
	mPipelineMaps[kDynamicPipelineHexShieldsLighting][crc] = pPipeline;
}

} // namespace engine

#endif // BT_CLIENT
