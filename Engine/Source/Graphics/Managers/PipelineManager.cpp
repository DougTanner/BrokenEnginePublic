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
, mWorldLightingShadowPipelines(mShaders, mpPipelines, mSpreadPipelines, mSpreadPipelineNames, mCombinePipeline, mLightingTemporalPipeline, mLightingHistoryCopyPipeline, mppWaterNormalTextures)
{
	ASSERT(gpPipelineManager == nullptr);

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

		// Trust boundary: the descriptor-binding / vertex-attribute counts come from on-disk pack bytes and drive
		// reinterpret_cast offsets + indexed walks aliasing the eager shader chunk. A negative or oversized count
		// would walk the alias pointers off the chunk, so reject against the structural maxima before the sizes are
		// computed. Boot-required shader; a throw propagates to MainThread's try/catch (HandleException — crash
		// report + exit) — boot hard-fail.
		if (rShaderHeader.iDescriptorSetLayoutBindings < 0
			|| rShaderHeader.iDescriptorSetLayoutBindings > common::ShaderHeader::kiMaxDescriptorSetLayoutBindings
			|| rShaderHeader.iVertexInputAttributeDescriptions < 0
			|| rShaderHeader.iVertexInputAttributeDescriptions > common::ShaderHeader::kiMaxVertexInputAttributeDescriptions)
		{
			char pcHex[20] {};
			LOG(kLoading, kError, "Corrupt shader chunk {}: implausible binding/attribute counts {} / {}", common::ToHex(std::span(pcHex), rCrc), rShaderHeader.iDescriptorSetLayoutBindings, rShaderHeader.iVertexInputAttributeDescriptions);
			throw common::CorruptStreamException("PipelineManager shader");
		}

		const int64_t iSetIndicesOffset = common::ShaderHeader::SetIndicesOffset(rShaderHeader.iDescriptorSetLayoutBindings);
		const int64_t iAttributesOffset = common::ShaderHeader::AttributesOffset(rShaderHeader.iDescriptorSetLayoutBindings);
		const int64_t iSpirvOffset = common::ShaderHeader::SpirvOffset(rShaderHeader.iDescriptorSetLayoutBindings, rShaderHeader.iVertexInputAttributeDescriptions);

		// Trust boundary (chunk bytes): the three aliased sections plus the SPIR-V tail (>= the 4-byte magic the
		// Shader ctor reads) must fit the chunk's actual bytes, else the alias walks / iSpirvSize run off the buffer.
		if (iSpirvOffset + static_cast<int64_t>(sizeof(uint32_t)) > rChunk.pHeader->iSize)
		{
			char pcHex[20] {};
			LOG(kLoading, kError, "Corrupt shader chunk {}: section extent exceeds chunk bytes", common::ToHex(std::span(pcHex), rCrc));
			throw common::CorruptStreamException("PipelineManager shader");
		}

		ShaderInfo info
		{
			.pChunkHeader = rChunk.pHeader,
			.pDescriptorBindings = reinterpret_cast<const VkDescriptorSetLayoutBinding*>(rChunk.pData),
			.pDescriptorSetIndices = reinterpret_cast<const uint32_t*>(rChunk.pData + iSetIndicesOffset),
			.pVertexAttributes = reinterpret_cast<const VkVertexInputAttributeDescription*>(rChunk.pData + iAttributesOffset),
			.iSpirvSize = rChunk.pHeader->iSize - iSpirvOffset,
		};
		auto [it, bInserted] = mShaders.try_emplace(rCrc, info, rChunk.pData + iSpirvOffset);
		ASSERT(bInserted);
	}

	// Generate BRDF LUT texture before creating model pipelines that reference it
	gpTextureManager->mTextureCache.GeneratePbrLutBrdf();

	// Clear stale pipeline pointers before pipelines are recreated
	gpTextureManager->mTextureDescriptors.ClearTextureBindings();

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
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kMainLayoutUniformBuffers},
			},
		});
	}

	CreateTerrainDataPipelines();

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
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTextures},
				{.flags = kCombinedSamplers, .iCount = 3, .ppTextures = rTextures.mppLightingDepositTextures},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTexturesB},
				{.flags = kCombinedSamplers, .iCount = shaders::kiMaxDebugTextures, .ppTextures = rTextures.mppDebugTexturesC},
			},
		});
	}

	CreateSmokeWindPipelines();

	CreateParticlePipelines();

	// HDR resolve: fullscreen quad sampling the F16 scene intermediate, tone-mapping + color-grading into the
	// swapchain (mVkRenderPass). kRenderTarget forces samples=1 to match the single-sample present pass.
	mpPipelines[kPipelineHdrResolve].Create(
	{
		.name = "HdrResolve",
		.flags = {kRenderTarget, kNoWireframe},
		.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersHdrResolvefragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = gpSwapchainManager->mVkRenderPass,
		.vkExtent3D = gpSwapchainManager->mHdrTexture.mInfo.extent,
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kMainLayoutUniformBuffers},
			{.flags = kCombinedSamplers, .pTexture = &gpSwapchainManager->mHdrTexture},
		},
	});

	CreateDebugRenderPipelines();

	game::FrameInterpolate::GraphicsResources();
}

PipelineManager::~PipelineManager()
{
	if (gpPipelineManager == this)
	{
		gpPipelineManager = nullptr;
	}
}

void PipelineManager::CreateLightingPipelines()
{
	mWorldLightingShadowPipelines.CreateLightingPipelines();
}

void PipelineManager::CreatePipelineShadows()
{
	mWorldLightingShadowPipelines.CreatePipelineShadows();
}

void PipelineManager::CreateLightingBlurPipelines()
{
	mWorldLightingShadowPipelines.CreateLightingBlurPipelines();
}

void PipelineManager::CreateLightingShadowDependentPipelines()
{
	mWorldLightingShadowPipelines.CreateLightingShadowDependentPipelines();
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
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpIslands->mIslandsStorageBuffers.data()},
			// kSamplerElevation for R16_SFLOAT bindless heightmap (unconditionally LINEAR — spec-mandated for 16-bit-float formats). See TextureManager::CreateSamplers.
			{.flags = {kCombinedSamplers, kSamplerElevation, kBindlessArrayConsumer}, .iCount = shaders::kiMaxIslands, .ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data()}, // set=1 binding 2 (elevation, kPipelineTerrainElevation)
		},
	});
}

void PipelineManager::CreateSmokeWindPipelines()
{
	struct SmokeClearPipelineDescription
	{
		Pipelines ePipeline;
		std::string_view name;
		Texture* pTargetTexture;
	};
	SmokeClearPipelineDescription pSmokeClearPipelineDescriptions[]
	{
		{.ePipeline = kPipelineSmokeClearA, .name = "SmokeClearA", .pTargetTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
		{.ePipeline = kPipelineSmokeClearB, .name = "SmokeClearB", .pTargetTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
	};
	for (const SmokeClearPipelineDescription& rDescription : pSmokeClearPipelineDescriptions)
	{
		mpPipelines[rDescription.ePipeline].Create(
		{
			.name = rDescription.name,
			.flags = {kRenderTarget, kPushConstants, kIndirectHostVisible},
			.ppShaders = {&mShaders.at(data::kShadersQuadsQuadsFullscreenvertCrc), &mShaders.at(data::kShadersClearfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.vkRenderPass = rDescription.pTargetTexture->mVkRenderPass,
			.vkExtent3D = rDescription.pTargetTexture->mInfo.extent,
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
			},
		});
	}

	gpBufferManager->CreateSmokeHierarchicalBuffers();

	struct SmokeOccupancyPipelineDescription
	{
		Pipelines ePipeline;
		std::string_view name;
		common::crc_t shaderCrc;
		VkBuffer* pSourceOccupancyVkBuffer;
		VkBuffer* pActiveTileVkBuffer;
		VkBuffer* pDestinationOccupancyVkBuffer;
	};
	SmokeOccupancyPipelineDescription pSmokeOccupancyPipelineDescriptions[]
	{
		{.ePipeline = kPipelineSmokeOccupancyDilate, .name = "SmokeOccupancyDilate", .shaderCrc = data::kShadersSmokeSmokeOccupancyDilatecompCrc, .pSourceOccupancyVkBuffer = &gpBufferManager->mSmokeOccupancyVkBuffers[0], .pActiveTileVkBuffer = &gpBufferManager->mSmokeActiveTileVkBuffer, .pDestinationOccupancyVkBuffer = &gpBufferManager->mSmokeOccupancyVkBuffers[1]},
		{.ePipeline = kPipelineSmokeOccupancyDilateRemap, .name = "SmokeOccupancyDilateRemap", .shaderCrc = data::kShadersSmokeSmokeOccupancyDilateRemapcompCrc, .pSourceOccupancyVkBuffer = &gpBufferManager->mSmokeOccupancyVkBuffers[1], .pActiveTileVkBuffer = &gpBufferManager->mSmokeActiveTileVkBuffer, .pDestinationOccupancyVkBuffer = &gpBufferManager->mSmokeOccupancyVkBuffers[0]},
	};
	for (const SmokeOccupancyPipelineDescription& rDescription : pSmokeOccupancyPipelineDescriptions)
	{
		mpPipelines[rDescription.ePipeline].Create(
		{
			.name = rDescription.name,
			.flags = {kCompute},
			.ppShaders = {&mShaders.at(rDescription.shaderCrc)},
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pSourceOccupancyVkBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pActiveTileVkBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pDestinationOccupancyVkBuffer},
			},
		});
	}

	mpPipelines[kPipelineSmokeSpreadComputeB].Create(
	{
		.name = "SmokeSpreadComputeB",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeSpreadTwocompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .textureCrc = data::kTexturesSmokeBC4tex_glass_0001_MKjpgCrc},
			{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[1]},
		},
	});

	mpPipelines[kPipelineSmokeSpreadComputeA].Create(
	{
		.name = "SmokeSpreadComputeA",
		.flags = {kCompute, kUpdateAfterBind},
		.ppShaders = {&mShaders.at(data::kShadersSmokeSmokeSpreadOnecompCrc)},
		.pDescriptorInfos =
		{
			{.flags = kGlobalLayoutUniformBuffers},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageImages, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[0]},
		},
	});

	gpBufferManager->CreateWindHierarchicalBuffers();

	struct WindOccupancyPipelineDescription
	{
		Pipelines ePipeline;
		std::string_view name;
		VkBuffer* pSourceOccupancyVkBuffer;
		VkBuffer* pActiveTileVkBuffer;
	};
	WindOccupancyPipelineDescription pWindOccupancyPipelineDescriptions[]
	{
		{.ePipeline = kPipelineWindOccupancyDilateA, .name = "WindOccupancyDilateA", .pSourceOccupancyVkBuffer = &gpBufferManager->mWindOccupancyVkBuffers[1], .pActiveTileVkBuffer = &gpBufferManager->mWindActiveTileVkBuffers[0]},
		{.ePipeline = kPipelineWindOccupancyDilateB, .name = "WindOccupancyDilateB", .pSourceOccupancyVkBuffer = &gpBufferManager->mWindOccupancyVkBuffers[0], .pActiveTileVkBuffer = &gpBufferManager->mWindActiveTileVkBuffers[1]},
	};
	for (const WindOccupancyPipelineDescription& rDescription : pWindOccupancyPipelineDescriptions)
	{
		mpPipelines[rDescription.ePipeline].Create(
		{
			.name = rDescription.name,
			.flags = {kCompute},
			.ppShaders = {&mShaders.at(data::kShadersWindWindOccupancyDilatecompCrc)},
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pSourceOccupancyVkBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pActiveTileVkBuffer},
			},
		});
	}

	struct WindSpreadPipelineDescription
	{
		Pipelines ePipeline;
		std::string_view name;
		common::crc_t shaderCrc;
		Texture* pSourceTexture;
		Texture* pDestinationTexture;
		VkBuffer* pActiveTileVkBuffer;
		VkBuffer* pOccupancyVkBuffer;
	};
	WindSpreadPipelineDescription pWindSpreadPipelineDescriptions[]
	{
		{.ePipeline = kPipelineWindSpreadComputeA, .name = "WindSpreadComputeA", .shaderCrc = data::kShadersWindWindSpreadOnecompCrc, .pSourceTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo, .pDestinationTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne, .pActiveTileVkBuffer = &gpBufferManager->mWindActiveTileVkBuffers[0], .pOccupancyVkBuffer = &gpBufferManager->mWindOccupancyVkBuffers[0]},
		{.ePipeline = kPipelineWindSpreadComputeB, .name = "WindSpreadComputeB", .shaderCrc = data::kShadersWindWindSpreadTwocompCrc, .pSourceTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne, .pDestinationTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo, .pActiveTileVkBuffer = &gpBufferManager->mWindActiveTileVkBuffers[1], .pOccupancyVkBuffer = &gpBufferManager->mWindOccupancyVkBuffers[1]},
	};
	for (const WindSpreadPipelineDescription& rDescription : pWindSpreadPipelineDescriptions)
	{
		mpPipelines[rDescription.ePipeline].Create(
		{
			.name = rDescription.name,
			.flags = {kCompute, kUpdateAfterBind},
			.ppShaders = {&mShaders.at(rDescription.shaderCrc)},
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = {kCombinedSamplers, kSamplerWindClamp}, .pTexture = rDescription.pSourceTexture},
				{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
				{.flags = kStorageImages, .pTexture = rDescription.pDestinationTexture},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pActiveTileVkBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = rDescription.pOccupancyVkBuffer},
			},
		});
	}
}

void PipelineManager::CreateParticlePipelines()
{
	struct ParticlePipelineDescription
	{
		Pipelines eUpdatePipeline;
		Pipelines eRenderPipeline;
		Pipelines eSpawnPipeline;
		std::string_view updateName;
		std::string_view renderName;
		std::string_view spawnName;
		Buffer* pStorageBuffer;
		Buffer* pSpawnStorageBuffers;
		common::crc_t renderVertexShaderCrc;
	};
	ParticlePipelineDescription pParticlePipelineDescriptions[]
	{
		{.eUpdatePipeline = kPipelineLongParticlesUpdate, .eRenderPipeline = kPipelineLongParticlesRender, .eSpawnPipeline = kPipelineLongParticlesSpawn, .updateName = "LongParticlesUpdate", .renderName = "LongParticlesRender", .spawnName = "LongParticlesSpawn", .pStorageBuffer = &gpBufferManager->mLongParticlesStorageBuffer, .pSpawnStorageBuffers = gpBufferManager->mLongParticlesSpawnStorageBuffers.data(), .renderVertexShaderCrc = data::kShadersParticlesLongParticlesRendervertCrc},
		{.eUpdatePipeline = kPipelineSquareParticlesUpdate, .eRenderPipeline = kPipelineSquareParticlesRender, .eSpawnPipeline = kPipelineSquareParticlesSpawn, .updateName = "SquareParticlesUpdate", .renderName = "SquareParticlesRender", .spawnName = "SquareParticlesSpawn", .pStorageBuffer = &gpBufferManager->mSquareParticlesStorageBuffer, .pSpawnStorageBuffers = gpBufferManager->mSquareParticlesSpawnStorageBuffers.data(), .renderVertexShaderCrc = data::kShadersParticlesSquareParticlesRendervertCrc},
	};
	for (const ParticlePipelineDescription& rDescription : pParticlePipelineDescriptions)
	{
		mpPipelines[rDescription.eUpdatePipeline].Create(
		{
			.name = rDescription.updateName,
			.flags = {kCompute, kIndirectDeviceLocal},
			.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesUpdatecompCrc)},
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kStorageBuffer, .pBuffers = rDescription.pStorageBuffer},
				{.flags = kCombinedSamplers, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			},
		});

		mpPipelines[rDescription.eRenderPipeline].Create(
		{
			.name = rDescription.renderName,
			.flags = {kIndirectDeviceLocal, kDepthTest, kAdd, kUpdateAfterBind},
			.ppShaders = {&mShaders.at(rDescription.renderVertexShaderCrc), &mShaders.at(data::kShadersParticlesParticlesRenderfragCrc)},
			.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kMainLayoutUniformBuffers},
				{.flags = kStorageBuffer, .pBuffers = rDescription.pStorageBuffer},
				{.flags = {kCombinedSamplers, kSamplerSmoke}, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
				{.flags = kSamplerClamp},
				{.flags = kTextures},
			},
		});

		mpPipelines[rDescription.eSpawnPipeline].Create(
		{
			.name = rDescription.spawnName,
			.flags = {kCompute},
			.ppShaders = {&mShaders.at(data::kShadersParticlesParticlesSpawncompCrc)},
			.pDescriptorInfos =
			{
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = rDescription.pSpawnStorageBuffers},
				{.flags = kStorageBuffer, .pBuffers = rDescription.pStorageBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[rDescription.eUpdatePipeline].mIndirectVkBuffer},
				{.flags = kStorageBuffer, .pVkBuffers = &mpPipelines[rDescription.eRenderPipeline].mIndirectVkBuffer},
			},
		});
	}
}

void PipelineManager::CreateDebugRenderPipelines()
{
	if constexpr (!kbDebugRender)
	{
		return;
	}

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
				{.flags = kGlobalLayoutUniformBuffers},
				{.flags = kMainLayoutUniformBuffers},
				{.flags = kPerCommandBufferStorageBuffers, .pBuffers = gpBufferManager->mDynamicStorageBuffers[kBufferMain].at(rEntry.crc).data()},
			},
		});
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
