#include "GltfPipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"

namespace engine
{

void GltfPipeline::Create(common::crc_t gltfCrc, const PipelineInfo& rPipelineInfo, bool bAddGltfDescriptors)
{
	PipelineInfo pipelineInfo = rPipelineInfo;

	// Add Gltf flag to descriptors
	if (bAddGltfDescriptors)
	{
		for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
		{
			DescriptorInfo& rDescriptorInfo = pipelineInfo.pDescriptorInfos[i];
			if (rDescriptorInfo.flags & DescriptorFlags::kEmpty)
			{
				rDescriptorInfo.flags = DescriptorFlags::kGltf;
				rDescriptorInfo.crc = gltfCrc;
				break;
			}
		}
	}

	const EagerChunk& chunk = gpFileManager->GetEagerChunkMap().at(gltfCrc);
	miMaterialCount = chunk.pHeader->gltfHeader.uiMaterialCount;
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines[i].Create(pipelineInfo, true);

		mpiFirstIndices[i] = chunk.pHeader->gltfHeader.puiIndexStarts[i];
		mpiIndexCounts[i] = (i + 1 == chunk.pHeader->gltfHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : chunk.pHeader->gltfHeader.puiIndexStarts[i + 1]) - mpiFirstIndices[i];
	}
}

void GltfPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	XMFLOAT4 f4PushConstants = rf4PushConstants;

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		f4PushConstants.w = static_cast<float>(i);
		mpPipelines[i].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, f4PushConstants);
	}
}

void GltfPipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount)
{
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		mpPipelines[i].WriteIndirectBuffer(iCommandBuffer, iCount, mpiIndexCounts[i], mpiFirstIndices[i]);
	}
}

void GltfPipeline::UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		mpPipelines[i].UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
	}
}

// Re-write per-material combined image sampler descriptors using actual texture image views
void GltfPipeline::UpdateGltfTextureDescriptors()
{
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		Pipeline& rPipeline = mpPipelines[i];

		// Find the kGltf descriptor to get the glTF CRC and starting binding
		int64_t iGltfDescriptorIndex = -1;
		int64_t iStartingBinding = 0;
		for (int64_t j = 0; j < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++j)
		{
			const DescriptorInfo& rDescriptorInfo = rPipeline.mInfo.pDescriptorInfos[j];
			if (rDescriptorInfo.flags & DescriptorFlags::kEmpty)
			{
				break;
			}

			if (rDescriptorInfo.flags & DescriptorFlags::kGltf)
			{
				iGltfDescriptorIndex = j;
				break;
			}

			// Count bindings before the kGltf descriptor
			if (rDescriptorInfo.flags & DescriptorFlags::kTextures || rDescriptorInfo.flags & DescriptorFlags::kUiTextures)
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

		if (iGltfDescriptorIndex < 0)
		{
			continue;
		}

		const DescriptorInfo& rGltfDescriptor = rPipeline.mInfo.pDescriptorInfos[iGltfDescriptorIndex];
		const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(rGltfDescriptor.crc);
		common::GltfShaderData& rGltfData = reinterpret_cast<common::GltfShaderData*>(rChunk.pData)[rPipeline.mInfo.uiMaterialIndex];

		int64_t piTextureIndices[5] = {rGltfData.uiColorTextureIndex, rGltfData.uiPhysicalDescriptorTextureIndex, rGltfData.uiNormalTextureIndex, rGltfData.uiOcclusionTextureIndex, rGltfData.uiEmissiveTextureIndex};
		VkSampler vkSampler = gpTextureManager->GetSampler(DescriptorFlags::kSamplerRepeat);

		for (int64_t j = 0; j < 5; ++j)
		{
			common::crc_t textureCrc = rChunk.pHeader->gltfHeader.pTextureCrcs[piTextureIndices[j]];
			Texture& rTexture = gpTextureManager->mTextureMap.at(textureCrc);
			rPipeline.UpdateCombinedImageSamplerDescriptor(iStartingBinding + j, rTexture.mVkImageView, vkSampler);
		}
	}
}

} // namespace engine
