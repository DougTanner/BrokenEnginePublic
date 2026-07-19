#if defined(BT_CLIENT)

#include "Graphics/Managers/DynamicPipelines.h"

#include "Data/Model.h"
#include "Data/Shader.h"
#include "Data/Texture.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

DynamicPipelines::DynamicPipelines(std::unordered_map<common::crc_t, Shader>& rShaders)
: mrShaders(rShaders)
{
}

// Resolves a model scene CRC to its mesh CRC + animation-aware vertex shader CRC (shared by the model + model-shadow pipeline creators).
static void ResolveModelChunkShaders(common::crc_t sceneCrc, common::crc_t& rModelCrc, common::crc_t& rVertexShaderCrc)
{
	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	rModelCrc = rChunk.pHeader->sceneHeader.modelCrc;
	rVertexShaderCrc = rChunk.pHeader->sceneHeader.bHasAnimation ? data::kShadersModelModelSkinnedvertCrc : data::kShadersModelModelStaticvertCrc;
}

// Allocates a Pipeline, runs Create with the supplied info, and registers it under eType/crc — the boilerplate tail shared by every CreatePipeline* below.
void DynamicPipelines::AddPipeline(DynamicPipelineType eType, common::crc_t crc, const PipelineInfo& rPipelineInfo)
{
	mPipelines.push_back(std::make_unique<Pipeline>());
	Pipeline* pPipeline = mPipelines.back().get();
	pPipeline->Create(rPipelineInfo);
	mPipelineMaps[eType].insert_or_assign(crc, pPipeline);
}

ModelPipeline* DynamicPipelines::CreateModelPipeline(const ModelPipelineSpec& rModelPipelineSpec)
{
	std::unique_ptr<ModelPipeline> pModelPipeline = std::make_unique<ModelPipeline>();

	// Trust boundary: ModelPipeline::Create parses on-disk scene-header counts. A corrupt count throws;
	// boot-required model pipeline, so log kError and let it propagate to MainThread's try/catch
	// (HandleException — crash report + exit), matching the boot hard-fail tier.
	try
	{
		pModelPipeline->Create(rModelPipelineSpec.sceneCrc, rModelPipelineSpec.pipelineInfo, rModelPipelineSpec.bIsPipelineShadow);
	}
	catch (const common::CorruptStreamException& rException)
	{
		char pcHex[20] {};
		LOG(kLoading, kError, "Corrupt scene chunk for {}model pipeline \"{}\" (scene CRC {}): {}", rModelPipelineSpec.bIsPipelineShadow ? "shadow " : "", rModelPipelineSpec.pipelineInfo.name, common::ToHex(std::span(pcHex), rModelPipelineSpec.sceneCrc), rException.what());
		throw;
	}

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

	// Look up the model CRC and animation-aware vertex shader from the glTF header
	common::crc_t modelCrc = 0;
	common::crc_t vertexShaderCrc = 0;
	ResolveModelChunkShaders(sceneCrc, modelCrc, vertexShaderCrc);

	ModelPipeline* pPipeline = CreateModelPipeline(
	{
		.sceneCrc = sceneCrc,
		.pipelineInfo =
		{
			.name = name,
			.flags = {kIndirectHostVisible, kPushConstants, kDepthTest, kDepthWrite, kCullBack, kSampleShading, kUpdateAfterBind, kMultiSet},
			.ppShaders = {&mrShaders.at(vertexShaderCrc), &mrShaders.at(data::kShadersModelModelfragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelCrc),
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kMainLayoutUniformBuffers},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bIsPipelineShadow = false,
	});

	mModelPipelineMaps[kDynamicModelPipelineModel].insert_or_assign(crc, pPipeline);
}

void DynamicPipelines::CreateModelPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers)
{
	// Skip if shadow pipeline already exists
	if (mModelPipelineMaps[kDynamicModelPipelineModelShadow].contains(crc))
	{
		return;
	}

	// Look up the model CRC and animation-aware vertex shader from the glTF header
	common::crc_t modelCrc = 0;
	common::crc_t vertexShaderCrc = 0;
	ResolveModelChunkShaders(sceneCrc, modelCrc, vertexShaderCrc);

	// Create shadow variant of pipeline name (stored in map to outlive this function)
	std::string& rShadowName = mShadowPipelineNames.insert_or_assign(crc, std::string(name) + "Shadow").first->second;

	// Create shadow pipeline with minimal descriptor sets
	ModelPipeline* pPipelineShadow = CreateModelPipeline(
	{
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
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kMainLayoutUniformBuffers},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			},
		},
		.bIsPipelineShadow = true,
	});

	mModelPipelineMaps[kDynamicModelPipelineModelShadow].insert_or_assign(crc, pPipelineShadow);
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
	AddPipeline(kDynamicPipelineLighting, crc,
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mrShaders.at(data::kShadersLightingAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
		},
	});
}

void DynamicPipelines::CreatePipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers)
{
	// Skip if visible lights pipeline already exists
	if (mPipelineMaps[kDynamicPipelineVisibleLights].contains(crc))
	{
		return;
	}

	// Allocate pipeline for visible lights rendering in main pass
	AddPipeline(kDynamicPipelineVisibleLights, crc,
	{
		.name = name,
		.flags = {kIndirectHostVisible, kAddAlpha, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersLightingVisibleLightvertCrc), &mrShaders.at(data::kShadersLightingVisibleLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
		},
	});
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
	AddPipeline(kDynamicPipelineAxisAlignedLighting, crc,
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mrShaders.at(data::kShadersLightingPointLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});
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
	AddPipeline(kDynamicPipelineBillboards, crc,
	{
		.name = name,
		.flags = {kIndirectHostVisible, kSampleShading, kAlphaBlend, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersParticlesBillboardsvertCrc), &mrShaders.at(data::kShadersParticlesBillboardsfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});
}

void DynamicPipelines::CreateDepositPipeline(DynamicPipelineType eType, common::crc_t crc, std::string_view name, common::crc_t vertexShaderCrc, common::crc_t fragmentShaderCrc, Texture& rTargetTexture, const DescriptorInfo& rTextureDescriptor, VkBuffer* pOccupancyBuffer, int64_t iBufferSize)
{
	if (mPipelineMaps[eType].contains(crc))
	{
		return;
	}

	if (iBufferSize > 0)
	{
		gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);
	}

	AddPipeline(eType, crc,
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(vertexShaderCrc), &mrShaders.at(fragmentShaderCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = rTargetTexture.mVkRenderPass,
		.vkExtent3D = rTargetTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			rTextureDescriptor,
			{.flags = kStorageBuffer, .pVkBuffers = pOccupancyBuffer},
		},
	});
}

void DynamicPipelines::CreatePipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	CreateDepositPipeline(kDynamicPipelineSmokeAxisAligned, crc, name,
		data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc, data::kShadersSmokeSmokefragCrc,
		gpTextureManager->mRenderTargetTextures.mSmokeTextureOne,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .textureCrc = data::kTexturesSmokeBC44jpgCrc},
		&gpBufferManager->mSmokeOccupancyVkBuffers[0], iBufferSize);
}

void DynamicPipelines::CreatePipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	CreateDepositPipeline(kDynamicPipelineSmoke, crc, name,
		data::kShadersQuadsQuadsVisibleAreavertCrc, data::kShadersSmokeSmokefragCrc,
		gpTextureManager->mRenderTargetTextures.mSmokeTextureOne,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeGradientTexture},
		&gpBufferManager->mSmokeOccupancyVkBuffers[0], iBufferSize);
}

void DynamicPipelines::CreatePipelineWindDepositA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	CreateDepositPipeline(kDynamicPipelineWindDepositA, crc, name,
		data::kShadersQuadsQuadsVisibleAreavertCrc, data::kShadersWindWindDepositfragCrc,
		gpTextureManager->mRenderTargetTextures.mWindTextureOne,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		&gpBufferManager->mWindOccupancyVkBuffers[0], iBufferSize);
}

void DynamicPipelines::CreatePipelineWindDepositB(common::crc_t crc, std::string_view name)
{
	CreateDepositPipeline(kDynamicPipelineWindDepositB, crc, name,
		data::kShadersQuadsQuadsVisibleAreavertCrc, data::kShadersWindWindDepositfragCrc,
		gpTextureManager->mRenderTargetTextures.mWindTextureTwo,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		&gpBufferManager->mWindOccupancyVkBuffers[1], 0);
}

void DynamicPipelines::CreatePipelineWindDepositAxisAlignedA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	CreateDepositPipeline(kDynamicPipelineWindDepositAxisAlignedA, crc, name,
		data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc, data::kShadersWindWindDepositfragCrc,
		gpTextureManager->mRenderTargetTextures.mWindTextureOne,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .textureCrc = data::kTexturesParticlesBC4Square24pngCrc},
		&gpBufferManager->mWindOccupancyVkBuffers[0], iBufferSize);
}

void DynamicPipelines::CreatePipelineWindDepositAxisAlignedB(common::crc_t crc, std::string_view name)
{
	CreateDepositPipeline(kDynamicPipelineWindDepositAxisAlignedB, crc, name,
		data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc, data::kShadersWindWindDepositfragCrc,
		gpTextureManager->mRenderTargetTextures.mWindTextureTwo,
		{.flags = {kCombinedSamplers, kSamplerClamp}, .textureCrc = data::kTexturesParticlesBC4Square24pngCrc},
		&gpBufferManager->mWindOccupancyVkBuffers[1], 0);
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
	AddPipeline(kDynamicPipelineHexShields, crc,
	{
		.name = name,
		.flags = {kIndirectHostVisible, kPushConstants, kAlphaBlend, kDepthTest, kCullBack, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersObjectsHexShieldvertCrc), &mrShaders.at(data::kShadersObjectsHexShieldfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronDualGeodesicIcosahedrongltfMODELCrc),
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kCombinedSamplers, .textureCrc = data::kTexturesCSkyboxCrc},
		},
	});
}

void DynamicPipelines::CreatePipelineHexShieldsLighting(common::crc_t crc, std::string_view name)
{
	// Skip if HexShields lighting pipeline already exists
	if (mPipelineMaps[kDynamicPipelineHexShieldsLighting].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for HexShields lighting pass (shares buffer with main HexShields pipeline)
	AddPipeline(kDynamicPipelineHexShieldsLighting, crc,
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kMax, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mrShaders.at(data::kShadersObjectsHexShieldvertCrc), &mrShaders.at(data::kShadersObjectsHexShieldLightingfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronDualGeodesicIcosahedrongltfMODELCrc),
		.vkRenderPass = gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
		},
	});
}

void DynamicPipelines::UpdateAllModelPipelineDescriptors(int64_t iCommandBuffer, int64_t iBinding, Buffer* pBuffer)
{
	for (auto& [rCrc, rpPipeline] : mModelPipelineMaps[kDynamicModelPipelineModel])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, iBinding, pBuffer);
	}
	for (auto& [rCrc, rpPipeline] : mModelPipelineMaps[kDynamicModelPipelineModelShadow])
	{
		rpPipeline->UpdateStorageBufferDescriptors(iCommandBuffer, iBinding, pBuffer);
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
