#include "GltfPipeline.h"

#include "File/FileManager.h"

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

	Chunk& chunk = gpFileManager->GetDataChunkMap().at(gltfCrc);
	miMaterialCount = chunk.pHeader->gltfHeader.uiMaterialCount;
	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines[i].Create(pipelineInfo, true);

		mpiFirstIndices[i] = chunk.pHeader->gltfHeader.puiIndexStarts[i];
		mpiIndexCounts[i] = (i + 1 == chunk.pHeader->gltfHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : chunk.pHeader->gltfHeader.puiIndexStarts[i + 1]) - mpiFirstIndices[i];
	}
}

void GltfPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const DirectX::XMFLOAT4& rf4PushConstants)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	DirectX::XMFLOAT4 f4PushConstants = rf4PushConstants;

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

} // namespace engine
