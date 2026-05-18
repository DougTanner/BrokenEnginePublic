#include "ModelPipeline.h"

namespace engine
{

void ModelPipeline::Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bAddModelDescriptors, bool bIsShadow)
{
	// Snapshot inputs so Recreate() can reproduce them. Must capture before any mutation (the kModel
	// flag is appended into the local copy below — storing the post-mutation copy would trip the
	// kEmpty ASSERT in Pipeline::Create on the second pass).
	mInfoTemplate = rPipelineInfo;
	mbAddModelDescriptors = bAddModelDescriptors;
	mbIsShadow = bIsShadow;

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

	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	const common::SceneHeader& rSceneHeader = rChunk.pHeader->sceneHeader;
	miMaterialCount = rSceneHeader.uiMaterialCount;

	// Scene chunk data layout: [textureCrcs ALIGN16] [indexStarts ALIGN16] [MaterialShaderData]
	int64_t iTextureArraySize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)));
	int64_t iIndexStartsSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSceneHeader.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
	int64_t iSceneArraysSize = iTextureArraySize + iIndexStartsSize;
	const uint32_t* puiIndexStarts = reinterpret_cast<const uint32_t*>(rChunk.pData + iTextureArraySize);
	const common::MaterialShaderData* pMaterials = reinterpret_cast<const common::MaterialShaderData*>(rChunk.pData + iSceneArraysSize);
	PipelineFlags_t originalFlags = pipelineInfo.flags;

	bool bMultiSet = pipelineInfo.flags & PipelineFlags::kMultiSet;

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

		// All pipelines use global Set 0
		mpPipelines[i].mVkExternalDescriptorSetLayout = gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout;

		// Multi-set: inner Pipelines 1..N share first Pipeline's Set 1 layout
		if (bMultiSet && i > 0)
		{
			mpPipelines[i].mVkExternalDescriptorSetLayoutSet1 = mpPipelines[0].mVkDescriptorSetLayout;
		}

		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines[i].Create(pipelineInfo, true);

		mpiFirstIndices[i] = puiIndexStarts[i];
		mpiIndexCounts[i] = (i + 1 == rSceneHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : puiIndexStarts[i + 1]) - mpiFirstIndices[i];
	}
}

void ModelPipeline::Recreate()
{
	Create(mSceneCrc, mInfoTemplate, mbAddModelDescriptors, mbIsShadow);
}

void ModelPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants, ModelDrawPass ePass)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	XMFLOAT4 f4PushConstants = rf4PushConstants;

	// Multi-set: bind global Set 0 + shared Set 1 once before the material loop
	bool bMultiSet = mpPipelines[0].mInfo.flags & PipelineFlags::kMultiSet;
	if (bMultiSet)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iCommandBuffer], mpPipelines[0].mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mpPipelines[0].mVkPipelineLayout, 0, 2, sets, 0, nullptr);
		mpPipelines[0].mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	}

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
		if (bMultiSet)
		{
			mpPipelines[i].RecordDrawIndirectSet2(iCommandBuffer, vkCommandBuffer, f4PushConstants);
		}
		else
		{
			mpPipelines[i].RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, f4PushConstants);
		}
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
	if (mpPipelines[0].mInfo.flags & PipelineFlags::kMultiSet)
	{
		// Set 0 bindings (15, 16) are shared — only update the first pipeline's descriptor sets
		mpPipelines[0].UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
	}
	else
	{
		for (int64_t i = 0; i < miMaterialCount; ++i)
		{
			mpPipelines[i].UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
		}
	}
}

} // namespace engine
