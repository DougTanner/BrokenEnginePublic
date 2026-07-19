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
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
				{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
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
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpSwapchainManager->mHdrTexture},
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
	RenderTargetTextures& rTextures = gpTextureManager->mRenderTargetTextures;

	// Spread pipelines (radial directional spread, fragment shader with MRT)
	// Pass 0 reads deposit textures, passes 1+ read previous pass spread textures
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
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
	mCombinePipeline.Create(
	{
		.name = "LightCombine",
		.flags = {kCompute, kPushConstants},
		.ppShaders = {&mShaders.at(data::kShadersLightingLightCombinecompCrc)},
		.iPushConstantBytes = sizeof(shaders::CombinePushConstantsLayout),
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
			// kSamplerElevation: bindless source is R16_SFLOAT; sampler is unconditionally LINEAR (spec-mandated for 16-bit-float formats, see TextureManager::CreateSamplers).
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
		// stride and attribute layout come from shader reflection (Terrain.vert declares vec2 f2InPosition).
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = {kCombinedSamplers, kSamplerBorderWhite}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			// Bindless per-island color / normal / AO arrays.
			// Compositing fragment shader indexes these with the per-instance `uiTextureSlot` forwarded
			// from Terrain.vert; `kSamplerClamp` matches the per-slot RegisterTextureBinding flag in
			// IslandTerrainResidency.cpp so descriptor writes line up with the sampler descriptor layout.
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

	// Water. All three skybox-specular lobes are evaluated inline with analytic specular AA
	// (WATER_SPEC_AA_MODE in Water.frag) instead of MSAA sample shading — kSampleShading stays off.
	mpPipelines[kPipelineWater].Create(
	{
		.name = "Water",
		.flags = {kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kUpdateAfterBind, kIndirectHostVisible},
		.ppShaders = {&mShaders.at(data::kShadersWaterWatervertCrc), &mShaders.at(data::kShadersWaterWaterfragCrc)},
		.pVertexBuffer = &gpBufferManager->mWaterMeshBuffer,
		.pDescriptorInfos =
		{
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mMainLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures)), .ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures},
			{.flags = {kCombinedSamplers, kSamplerBorderWhite}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mObjectShadowsBlurTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = TextureManager::kPrefilteredWaterCrc},
			{.flags = {kCombinedSamplers, kSamplerRepeat}, .iCount = 1, .textureCrc = data::kTexturesWaterBC4NoisepngCrc},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeatWater}, .iCount = TextureManager::kiWaterNormalCount, .ppTextures = mppWaterNormalTextures},
			{.flags = kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesWaterDepthLutpngCrc},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = {kCombinedSamplers, kSamplerBorder}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mAmbientCombineTexture},
			// Compute-pre-computed Gerstner displacement + normal sampled in Water.vert via texelFetch.
			// Explicit bindings keep the shader-side numbers at 13/14 — binding 12 is intentionally
			// unused; renumbering the displacement bindings isn't worth the churn.
			{.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = shaders::kiWaterBindingDisplacement, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture},
			{.flags = kCombinedSamplers, .iCount = 1, .iExplicitBinding = shaders::kiWaterBindingDisplacementNormal, .pTexture = &gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture},
		},
	});
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
			// kSamplerElevation for R16_SFLOAT bindless heightmap (unconditionally LINEAR — spec-mandated for 16-bit-float formats). See TextureManager::CreateSamplers.
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
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[0]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[1]},
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
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[1]},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[0]},
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
			{.flags = kPerCommandBufferUniformBuffers, .pBuffers = gpBufferManager->mGlobalLayoutUniformBuffers.data()},
			{.flags = {kCombinedSamplers, kSamplerSmoke}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureTwo},
			{.flags = {kCombinedSamplers, kSamplerMirroredRepeat}, .iCount = 1, .textureCrc = data::kTexturesSmokeBC4tex_swirl_0002_MKjpgCrc},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureOne},
			{.flags = {kCombinedSamplers, kSamplerWindClamp}, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mWindTextureTwo},
			{.flags = kStorageImages, .iCount = 1, .pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeActiveTileVkBuffer},
			{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mSmokeOccupancyVkBuffers[0]},
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
