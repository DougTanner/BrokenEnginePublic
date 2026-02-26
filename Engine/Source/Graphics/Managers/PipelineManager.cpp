#ifdef BT_CLIENT

#include "Graphics/Managers/PipelineManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/TextureManager.h"

#include "Data/Model.h"
#include "Data/Shader.h"
#include "Data/Texture.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

PipelineManager::PipelineManager()
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
	gpTextureManager->GeneratePbrLutBrdf();

	// Clear stale pipeline pointers before pipelines are recreated
	gpTextureManager->ClearTextureBindings();

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
			.vkRenderPass = gpTextureManager->mLogTexture.mVkRenderPass,
			.vkExtent3D = gpTextureManager->mLogTexture.mInfo.extent,
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

ModelPipeline* PipelineManager::CreateModelPipeline(const ModelPipelineSpec& spec)
{
	std::unique_ptr<ModelPipeline> pModelPipeline = std::make_unique<ModelPipeline>();
	pModelPipeline->Create(spec.sceneCrc, spec.pipelineInfo, spec.bAddModelDescriptors, spec.bIsPipelineShadow);

	ModelPipeline* pResult = pModelPipeline.get();
	mDynamicModelPipelines.push_back(std::move(pModelPipeline));

	return pResult;
}

void PipelineManager::CreateDynamicModelPipeline(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers)
{
	// Skip if pipeline already exists
	if (mDynamicModelPipelineMaps[kDynamicModelPipelineModel].contains(crc))
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
			.ppShaders = {&mShaders.at(vertexShaderCrc), &mShaders.at(data::kShadersModelModelfragCrc)},
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

	mDynamicModelPipelineMaps[kDynamicModelPipelineModel][crc] = pPipeline;
}

void PipelineManager::CreateDynamicModelPipelineShadow(common::crc_t crc, std::string_view name, common::crc_t sceneCrc, Buffer* pStorageBuffers)
{
	// Skip if shadow pipeline already exists
	if (mDynamicModelPipelineMaps[kDynamicModelPipelineModelShadow].contains(crc))
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
			.ppShaders = {&mShaders.at(vertexShaderCrc), &mShaders.at(data::kShadersModelModelShadowfragCrc)},
			.pVertexBuffer = &gpBufferManager->mModelMap.at(modelCrc),
			.vkRenderPass = gpTextureManager->mObjectShadowsTexture.mVkRenderPass,
			.vkExtent3D = gpTextureManager->mObjectShadowsTexture.mInfo.extent,
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

	mDynamicModelPipelineMaps[kDynamicModelPipelineModelShadow][crc] = pPipelineShadow;
}

void PipelineManager::CreateDynamicPipelineLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if lighting pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineLighting].contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for area light rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mShaders.at(data::kShadersLightingAreaLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
		},
	});

	// Register pipeline in lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineLighting][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineVisibleLights(common::crc_t crc, std::string_view name, Buffer* pStorageBuffers)
{
	// Skip if visible lights pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineVisibleLights].contains(crc))
	{
		return;
	}

	// Allocate pipeline for visible lights rendering in main pass
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kAddAlpha, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersLightingVisibleLightvertCrc), &mShaders.at(data::kShadersLightingVisibleLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = pStorageBuffers},
			{.flags = kSamplerRepeat},
			{.flags = kTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
		},
	});

	// Register pipeline in visible lights map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineVisibleLights][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineAxisAlignedLighting(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if axis-aligned lighting pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineAxisAlignedLighting].contains(crc))
	{
		return;
	}

	// Create storage buffer for this lighting pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for point light rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersLightingPointLightfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
		},
	});

	// Register pipeline in axis-aligned lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineAxisAlignedLighting][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineBillboards(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if billboards pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineBillboards].contains(crc))
	{
		return;
	}

	// Create storage buffer for this billboards pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for billboard rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kSampleShading, kAlphaBlend, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersParticlesBillboardsvertCrc), &mShaders.at(data::kShadersParticlesBillboardsfragCrc)},
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
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineBillboards][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineSmokeAxisAligned(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if smoke axis-aligned pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineSmokeAxisAligned].contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for smoke puff rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC44jpgCrc},
		},
	});

	// Register pipeline in smoke axis-aligned map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineSmokeAxisAligned][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineSmoke(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if smoke pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineSmoke].contains(crc))
	{
		return;
	}

	// Create storage buffer for this smoke pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for smoke trail rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mShaders.at(data::kShadersSmokeSmokefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeGradientTexture},
		},
	});

	// Register pipeline in smoke map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineSmoke][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineWindDepositA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if wind deposit pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineWindDepositA].contains(crc))
	{
		return;
	}

	// Create storage buffer for this wind deposit pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for wind deposit rendering
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		},
	});

	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineWindDepositA][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineWindDepositB(common::crc_t crc, std::string_view name)
{
	// Skip if wind deposit B pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineWindDepositB].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for wind deposit rendering (texture B target)
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsVisibleAreavertCrc), &mShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesBC4Radial2pngCrc},
		},
	});

	Pipeline* pPipelineTwo = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineWindDepositB][crc] = pPipelineTwo;
}

void PipelineManager::CreateDynamicPipelineWindDepositAxisAlignedA(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	if (mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA].contains(crc))
	{
		return;
	}

	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesParticlesBC4Square16pngCrc},
		},
	});

	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedA][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineWindDepositAxisAlignedB(common::crc_t crc, std::string_view name)
{
	if (mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB].contains(crc))
	{
		return;
	}

	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible, kAdd, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersWindWindDepositfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
			{.flags = {kCombinedSamplers, kSamplerClamp}, .iCount = 1, .textureCrc = data::kTexturesParticlesBC4Square16pngCrc},
		},
	});

	Pipeline* pPipelineAxisAlignedTwo = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineWindDepositAxisAlignedB][crc] = pPipelineAxisAlignedTwo;
}

void PipelineManager::CreateDynamicPipelineHexShields(common::crc_t crc, std::string_view name, int64_t iBufferSize)
{
	// Skip if HexShields pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineHexShields].contains(crc))
	{
		return;
	}

	// Create storage buffer for this HexShields pipeline
	gpBufferManager->CreateDynamicBuffer(crc, kBufferMain, name, iBufferSize);

	// Allocate pipeline and configure for HexShields rendering (uses DualGeodesicIcosahedron mesh)
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kIndirectHostVisible, kPushConstants, kAlphaBlend, kDepthTest, kCullBack, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersObjectsHexShieldvertCrc), &mShaders.at(data::kShadersObjectsHexShieldfragCrc)},
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
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineHexShields][crc] = pPipeline;
}

void PipelineManager::CreateDynamicPipelineHexShieldsLighting(common::crc_t crc, std::string_view name)
{
	// Skip if HexShields lighting pipeline already exists
	if (mDynamicPipelineMaps[kDynamicPipelineHexShieldsLighting].contains(crc))
	{
		return;
	}

	// Allocate pipeline and configure for HexShields lighting pass (shares buffer with main HexShields pipeline)
	size_t iPipelineIndex = mDynamicPipelines.size();
	mDynamicPipelines.push_back(std::make_unique<Pipeline>());
	mDynamicPipelines[iPipelineIndex]->Create(
	{
		.name = name,
		.flags = {kRenderTarget, kPushConstants, kMax, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersObjectsHexShieldvertCrc), &mShaders.at(data::kShadersObjectsHexShieldLightingfragCrc)},
		.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kModelsDualGeodesicIcosahedronDualGeodesicIcosahedrongltfMODELCrc),
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(crc).data()},
		},
	});

	// Register pipeline in HexShields lighting map for iteration during rendering
	Pipeline* pPipeline = mDynamicPipelines[iPipelineIndex].get();
	mDynamicPipelineMaps[kDynamicPipelineHexShieldsLighting][crc] = pPipeline;
}

void PipelineManager::CreateLightingBlurCombinePipelines(Pipelines eCombinePipeline, Texture* pLightingTexture, Pipeline (&pLightingBlurPipelines)[shaders::kiMaxLightingBlurCount], Texture (&pLightingBlurTextures)[shaders::kiMaxLightingBlurCount])
{
	for (int64_t i = 0; i < gpTextureManager->miLightingBlurCount; ++i)
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
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
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
		.vkRenderPass = gpTextureManager->mLightingVkRenderPass,
		.vkExtent3D = gpTextureManager->mpLightingTextures[0].mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpBufferManager->mSquareParticlesStorageBuffer},
			{.flags = kSamplerClamp},
			{.flags = kTextures},
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
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mShadowElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mShadowElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mElevationTextures.data()},
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
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowElevationTexture},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mShadowTexture},
		},
	});

	mpPipelines[kPipelineShadowBlur].Create(
	{
		.name = "ShadowBlur",
		.flags = {kCompute},
		.ppShaders = {&mShaders.at(data::kShadersShadowShadowBlurcompCrc)},
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
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersShadowObjectShadowsBlurfragCrc)},
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
		.flags = {kDepthTest, kDepthWrite, kCullBack, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersTerrainTerrainvertCrc), &mShaders.at(data::kShadersTerrainTerrainfragCrc)},
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
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kSampleShading, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersWaterWatervertCrc), &mShaders.at(data::kShadersWaterWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mppLightingFinalTextures)), .ppTextures = gpTextureManager->mppLightingFinalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesCRyfjallet_PrefilteredR16G16B16A16_SFLOATCrc},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc}, // 4 8
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC70pngCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC73jpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
		},
	});
}

void PipelineManager::CreateTerrainDataPipelines()
{
	mpPipelines[kPipelineTerrainElevation].Create(
	{
		.name = "TerrainElevation",
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainElevationfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainElevationTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainElevationTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mElevationTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainColor].Create(
	{
		.name = "TerrainColor",
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainColorfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainColorTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainColorTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mColorTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainNormal].Create(
	{
		.name = "TerrainNormal",
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainNormalfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainNormalTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainNormalTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mNormalsTextures.data()},
		},
	});

	mpPipelines[kPipelineTerrainAmbientOcclusion].Create(
	{
		.name = "TerrainAmbientOcclusion",
		.flags = {kRenderTarget, kPushConstants, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedVisibleAreavertCrc), &mShaders.at(data::kShadersTerrainTerrainAmbientOcclusionfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mTerrainAmbientOcclusionTexture.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mTerrainAmbientOcclusionTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kStorageBuffer, .pBuffers = &gpIslands->mIslandsStorageBuffer},
			{.flags = kCombinedSamplers, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mAmbientOcclusionTextures.data()},
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
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
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
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
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
		.vkRenderPass = gpTextureManager->mSmokeTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureTwo},
		},
	});

	mpPipelines[kPipelineSmokeSpreadA].Create(
	{
		.name = "SmokeSpreadA",
		.flags = {kRenderTarget, kIndirectHostVisible, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsAxisAlignedvertCrc), &mShaders.at(data::kShadersSmokeSmokeSpreadOnefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mSmokeTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mSmokeSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureTwo},
		},
	});

	mpPipelines[kPipelineWindClearA].Create(
	{
		.name = "WindClearA",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureOne.mInfo.extent,
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
		.vkRenderPass = gpTextureManager->mWindTextureOne.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureOne.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mWindSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
		},
	});
	mpPipelines[kPipelineWindClearB].Create(
	{
		.name = "WindClearB",
		.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpTextureManager->mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureTwo.mInfo.extent,
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
		.vkRenderPass = gpTextureManager->mWindTextureTwo.mVkRenderPass,
		.vkExtent3D = gpTextureManager->mWindTextureTwo.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mWindSpreadStorageBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureOne},
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
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureTwo},
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
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
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
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mWindTextureTwo},
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
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mSmokeTextureOne},
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
		VkRenderPass vkLightingRenderPass = gpTextureManager->mLightingVkRenderPass;
		VkExtent3D vkLightingExtent = gpTextureManager->mpLightingTextures[0].mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineLighting, kDynamicPipelineAxisAlignedLighting, kDynamicPipelineHexShieldsLighting})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelineMaps[eType])
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
		VkRenderPass vkSmokeRenderPass = gpTextureManager->mSmokeTextureOne.mVkRenderPass;
		VkExtent3D vkSmokeExtent = gpTextureManager->mSmokeTextureOne.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineSmokeAxisAligned, kDynamicPipelineSmoke})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkSmokeRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkSmokeExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}

		VkRenderPass vkWindOneRenderPass = gpTextureManager->mWindTextureOne.mVkRenderPass;
		VkExtent3D vkWindOneExtent = gpTextureManager->mWindTextureOne.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineWindDepositA, kDynamicPipelineWindDepositAxisAlignedA})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelineMaps[eType])
			{
				rpPipeline->mInfo.vkRenderPass = vkWindOneRenderPass;
				rpPipeline->mInfo.vkExtent3D = vkWindOneExtent;
				rpPipeline->Create(rpPipeline->mInfo);
			}
		}

		VkRenderPass vkWindTwoRenderPass = gpTextureManager->mWindTextureTwo.mVkRenderPass;
		VkExtent3D vkWindTwoExtent = gpTextureManager->mWindTextureTwo.mInfo.extent;
		for (DynamicPipelineType eType : {kDynamicPipelineWindDepositB, kDynamicPipelineWindDepositAxisAlignedB})
		{
			for (auto& [rCrc, rpPipeline] : mDynamicPipelineMaps[eType])
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
		for (auto& [rCrc, rpPipeline] : mDynamicPipelineMaps[kDynamicPipelineVisibleLights])
		{
			rpPipeline->Create(rpPipeline->mInfo);
		}
	}
}

} // namespace engine

#endif // BT_CLIENT
