#include "ModelPipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

void ModelPipeline::Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors)
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
	miMaterialCount = chunk.pHeader->sceneHeader.uiMaterialCount;
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines[i].Create(pipelineInfo, true);

		mpiFirstIndices[i] = chunk.pHeader->sceneHeader.puiIndexStarts[i];
		mpiIndexCounts[i] = (i + 1 == chunk.pHeader->sceneHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : chunk.pHeader->sceneHeader.puiIndexStarts[i + 1]) - mpiFirstIndices[i];
	}
}

void ModelPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	XMFLOAT4 f4PushConstants = rf4PushConstants;

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
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
		const common::SceneHeader& rHeader = rChunk.pHeader->sceneHeader;
		gpFileManager->RequestChunkLoad(std::span(rHeader.pTextureCrcs, rHeader.uiTextureCount));
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
		common::MaterialShaderData& rMaterialData = reinterpret_cast<common::MaterialShaderData*>(rChunk.pData)[rPipeline.mInfo.uiMaterialIndex];

		int64_t piTextureIndices[5] = {rMaterialData.uiColorTextureIndex, rMaterialData.uiPhysicalDescriptorTextureIndex, rMaterialData.uiNormalTextureIndex, rMaterialData.uiOcclusionTextureIndex, rMaterialData.uiEmissiveTextureIndex};
		VkSampler vkSampler = gpTextureManager->GetSampler(DescriptorFlags::kSamplerRepeat);

		for (int64_t j = 0; j < 5; ++j)
		{
			common::crc_t textureCrc = rChunk.pHeader->sceneHeader.pTextureCrcs[piTextureIndices[j]];
			Texture& rTexture = gpTextureManager->mTextureMap.at(textureCrc);
			rPipeline.UpdateCombinedImageSamplerDescriptor(iStartingBinding + j, rTexture.mVkImageView, vkSampler);
		}
	}
}

} // namespace engine
