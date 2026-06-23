#if defined(BT_CLIENT)

#include "ModelPipeline.h"

namespace engine
{

void ModelPipeline::Create(common::crc_t sceneCrc, const PipelineInfo& rPipelineInfo, bool bIsShadow)
{
	PipelineInfo pipelineInfo = rPipelineInfo;

	// Add Model flag to the first empty descriptor slot
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

	mSceneCrc = sceneCrc;

	const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(sceneCrc);
	const common::SceneHeader& rSceneHeader = rChunk.pHeader->sceneHeader;

	// Trust boundary: SceneHeader counts come from on-disk pack bytes and drive per-material vector sizing,
	// the material loop bound, and reinterpret_cast pointer offsets (texture CRC + index-start arrays) into the
	// eager pack buffer. A corrupt/tampered count would size a hostile allocation or walk the alias pointers
	// off the buffer, so reject against the structural maxima before any of them is used.
	// uiMaterialCount == 0 is rejected here (not just > max) so the trust boundary throws uniformly:
	// mpPipelines.at(0) is read unconditionally in RecordDrawIndirect / UpdateStorageBufferDescriptors, so a
	// zero count must fail through CorruptStreamException rather than a downstream ASSERT of a different type.
	if (rSceneHeader.uiTextureCount > common::SceneHeader::kiMaxTextures
		|| rSceneHeader.uiMaterialCount == 0 || rSceneHeader.uiMaterialCount > common::SceneHeader::kiMaxMaterials)
	{
		throw common::CorruptStreamException("ModelPipeline::Create");
	}

	miMaterialCount = rSceneHeader.uiMaterialCount;

	// Right-size the per-material arrays to the actual count (Pipeline is non-movable, so assign a fresh vector rather than resize)
	mpPipelines = std::vector<Pipeline>(miMaterialCount);
	mpiIndexCounts.resize(miMaterialCount);
	mpiFirstIndices.resize(miMaterialCount);
	mpbTransparentMaterials.resize(miMaterialCount);

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
		mpbTransparentMaterials.at(i) = bTransparent;
		if (bTransparent)
		{
			mFlags.Set(ModelPipelineFlags::kHasTransparentMaterials);
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
		mpPipelines.at(i).mVkExternalDescriptorSetLayout = gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout;

		// Multi-set: inner Pipelines 1..N share first Pipeline's Set 1 layout
		if (bMultiSet && i > 0)
		{
			mpPipelines.at(i).mVkExternalDescriptorSetLayoutSet1 = mpPipelines.at(0).mVkDescriptorSetLayout;
		}

		pipelineInfo.uiMaterialIndex = static_cast<uint32_t>(i);
		mpPipelines.at(i).Create(pipelineInfo, true);

		mpiFirstIndices.at(i) = puiIndexStarts[i];
		mpiIndexCounts.at(i) = (i + 1 == rSceneHeader.uiMaterialCount ? pipelineInfo.pVertexBuffer->mInfo.iCount : puiIndexStarts[i + 1]) - mpiFirstIndices.at(i);
	}
}

void ModelPipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& rf4PushConstants, ModelDrawPass ePass)
{
	ASSERT(rf4PushConstants.w == 0.0f);
	XMFLOAT4 f4PushConstants = rf4PushConstants;

	// Multi-set: bind global Set 0 + shared Set 1 once before the material loop
	bool bMultiSet = mpPipelines.at(0).mInfo.flags & PipelineFlags::kMultiSet;
	if (bMultiSet)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iCommandBuffer], mpPipelines.at(0).mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mpPipelines.at(0).mVkPipelineLayout, 0, 2, sets, 0, nullptr);
		mpPipelines.at(0).mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	}

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		// Skip materials that don't match the requested draw pass
		if (ePass == ModelDrawPass::kOpaque && mpbTransparentMaterials.at(i))
		{
			continue;
		}
		if (ePass == ModelDrawPass::kTransparent && !mpbTransparentMaterials.at(i))
		{
			continue;
		}

		f4PushConstants.w = static_cast<float>(i);
		if (bMultiSet)
		{
			mpPipelines.at(i).RecordDrawIndirectSet2(iCommandBuffer, vkCommandBuffer, f4PushConstants);
		}
		else
		{
			mpPipelines.at(i).RecordDrawIndirect(iCommandBuffer, vkCommandBuffer, f4PushConstants);
		}
	}
}

void ModelPipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iCount)
{
	if (iCount > 0 && !(mFlags & ModelPipelineFlags::kTexturesRequested))
	{
		mFlags.Set(ModelPipelineFlags::kTexturesRequested);
		const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(mSceneCrc);
		const common::crc_t* pTextureCrcs = reinterpret_cast<const common::crc_t*>(rChunk.pData);
		gpFileManager->RequestChunkLoad(std::span(pTextureCrcs, rChunk.pHeader->sceneHeader.uiTextureCount));
	}

	for (int64_t i = 0; i < miMaterialCount; ++i)
	{
		mpPipelines.at(i).WriteIndirectBuffer(iCommandBuffer, iCount, mpiIndexCounts.at(i), mpiFirstIndices.at(i));
	}
}

void ModelPipeline::UpdateStorageBufferDescriptors(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	if (mpPipelines.at(0).mInfo.flags & PipelineFlags::kMultiSet)
	{
		// Set 0 bindings (15, 16) are shared — only update the first pipeline's descriptor sets
		mpPipelines.at(0).UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
	}
	else
	{
		for (int64_t i = 0; i < miMaterialCount; ++i)
		{
			mpPipelines.at(i).UpdateStorageBufferDescriptor(iFramebuffer, iBinding, pBuffer);
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
