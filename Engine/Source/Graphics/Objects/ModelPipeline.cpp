#include "ModelPipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

void ModelPipeline::Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors, bool bIsShadow)
{
	PipelineInfo pipelineInfo = rPipelineInfo;

	// Add Model flag to descriptors
	if (bAddModelDescriptors)
	{
		for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
		{
			DescriptorInfo& rDescriptorInfo = pipelineInfo.pDescriptorInfos[i];
			if (rDescriptorInfo.flags & DescriptorFlags::kEmpty)
			{
				rDescriptorInfo.flags = DescriptorFlags::kModel;
				rDescriptorInfo.crc = sceneCrc;
				break;
			}
		}
	}

	mSceneCrc = sceneCrc;

	const EagerChunk& chunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	const common::SceneHeader& rSceneHeader = chunk.pHeader->sceneHeader;
	miMaterialCount = rSceneHeader.uiMaterialCount;

	// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData]
	int64_t iTextureArraySize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)));
	int64_t iIndexStartsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
	int64_t iSceneArraysSize = iTextureArraySize + iIndexStartsSize;
	const uint32_t* puiIndexStarts = reinterpret_cast<const uint32_t*>(chunk.pData + iTextureArraySize);
	const common::MaterialShaderData* pMaterials = reinterpret_cast<const common::MaterialShaderData*>(chunk.pData + iSceneArraysSize);
	PipelineFlags_t originalFlags = pipelineInfo.flags;

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		// Detect transparent materials (fAlphaMask >= 2.0 signals BLEND alpha mode from export)
		bool bTransparent = pMaterials[i].fAlphaMask >= 2.0f;
		mpbTransparentMaterials[i] = bTransparent;
		if (bTransparent)
		{
			mbHasTransparentMaterials = true;
		}

		// Configure alpha blending for transparent non-shadow materials
		if (bTransparent && !bIsShadow)
		{
			pipelineInfo.flags = originalFlags;
			pipelineInfo.flags.Set(PipelineFlags::kAlphaBlend);
			// Some materials only have some transparent bits, and the rest still opaque
			// We will probably need to revisit this in the future once we have proper models
			// pipelineInfo.flags.Clear({PipelineFlags::kDepthWrite, PipelineFlags::kCullBack});
		}
		else
		{
			pipelineInfo.flags = originalFlags;
		}

		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines[i].Create(pipelineInfo, true);

		mpiFirstIndices[i] = puiIndexStarts[i];
		mpiIndexCounts[i] = (i + 1 == rSceneHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : puiIndexStarts[i + 1]) - mpiFirstIndices[i];
	}
}

void ModelPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants, ModelDrawPass ePass)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	XMFLOAT4 f4PushConstants = rf4PushConstants;

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		// Skip materials that don't match the requested draw pass
		if (ePass == ModelDrawPass::kOpaque && mpbTransparentMaterials[i])
		{
			continue;
		}
		if (ePass == ModelDrawPass::kTransparent && !mpbTransparentMaterials[i])
		{
			continue;
		}

		f4PushConstants.w = static_cast<float>(i);
		mpPipelines[i].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, f4PushConstants);
	}
}

void ModelPipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount)
{
	if (iCount > 0 && !mbTexturesRequested)
	{
		mbTexturesRequested = true;
		const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(mSceneCrc);
		const common::crc_t* pTextureCrcs = reinterpret_cast<const common::crc_t*>(rChunk.pData);
		gpFileManager->RequestChunkLoad(std::span(pTextureCrcs, rChunk.pHeader->sceneHeader.uiTextureCount));
	}

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		mpPipelines[i].WriteIndirectBuffer(iCommandBuffer, iCount, mpiIndexCounts[i], mpiFirstIndices[i]);
	}
}

void ModelPipeline::UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		mpPipelines[i].UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
	}
}

// Re-write per-material combined image sampler descriptors using actual texture image views
void ModelPipeline::UpdateModelTextureDescriptors()
{
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		Pipeline& rPipeline = mpPipelines[i];

		// Find the kModel descriptor to get the scene CRC and starting binding
		int64_t iModelDescriptorIndex = -1;
		int64_t iStartingBinding = 0;
		for (int64_t j = 0; j < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++j)
		{
			const DescriptorInfo& rDescriptorInfo = rPipeline.mInfo.pDescriptorInfos[j];
			if (rDescriptorInfo.flags & DescriptorFlags::kEmpty)
			{
				break;
			}

			if (rDescriptorInfo.flags & DescriptorFlags::kModel)
			{
				iModelDescriptorIndex = j;
				break;
			}

			// Count bindings before the kModel descriptor
			if (rDescriptorInfo.flags & DescriptorFlags::kTextures)
			{
				iStartingBinding += rDescriptorInfo.iCount;
			}
			else if (rDescriptorInfo.flags & DescriptorFlags::kCombinedSamplers)
			{
				iStartingBinding += rDescriptorInfo.iCount;
			}
			else
			{
				++iStartingBinding;
			}
		}

		if (iModelDescriptorIndex < 0)
		{
			continue;
		}

		const DescriptorInfo& rModelDescriptor = rPipeline.mInfo.pDescriptorInfos[iModelDescriptorIndex];
		const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(rModelDescriptor.crc);
		const common::SceneHeader& rSceneHeader = rChunk.pHeader->sceneHeader;

		// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData]
		const common::crc_t* pTextureCrcs = reinterpret_cast<const common::crc_t*>(rChunk.pData);
		int64_t iTextureArraySize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)));
		int64_t iIndexStartsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
		const common::MaterialShaderData& rMaterialData = reinterpret_cast<const common::MaterialShaderData*>(rChunk.pData + iTextureArraySize + iIndexStartsSize)[rPipeline.mInfo.uiMaterialIndex];

		int64_t piTextureIndices[5] = {rMaterialData.uiColorTextureIndex, rMaterialData.uiPhysicalDescriptorTextureIndex, rMaterialData.uiNormalTextureIndex, rMaterialData.uiOcclusionTextureIndex, rMaterialData.uiEmissiveTextureIndex};
		VkSampler vkSampler = gpTextureManager->GetSampler(DescriptorFlags::kSamplerRepeat);

		for (int64_t j = 0; j < 5; ++j)
		{
			common::crc_t textureCrc = pTextureCrcs[piTextureIndices[j]];
			Texture& rTexture = gpTextureManager->mTextureMap.at(textureCrc);
			rPipeline.UpdateCombinedImageSamplerDescriptor(iStartingBinding + j, rTexture.mVkImageView, vkSampler);
		}
	}
}

} // namespace engine
