#include "GltfPipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"

namespace engine
{

GltfPipeline::~GltfPipeline()
{
	// Free secondary command buffers allocated for this pipeline
	if (!mSecondaryBuffers.empty() && gpCommandBufferManager != nullptr && gpDeviceManager != nullptr)
	{
		for (int64_t iFramebuffer = 0; iFramebuffer < static_cast<int64_t>(mSecondaryBuffers.size()); ++iFramebuffer)
		{
			CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebuffer);
			if (mSecondaryBuffers[iFramebuffer] != VK_NULL_HANDLE)
			{
				vkFreeCommandBuffers(gpDeviceManager->mVkDevice, rCommandBuffers.mCommandPool, 1, &mSecondaryBuffers[iFramebuffer]);
			}
		}
	}
}

void GltfPipeline::AllocateSecondaryBuffers(const char* pcName)
{
	int64_t iFramebufferCount = static_cast<int64_t>(gpCommandBufferManager->mPerFramebufferCommandBuffers.size());
	mSecondaryBuffers.resize(iFramebufferCount);

	for (int64_t iFramebuffer = 0; iFramebuffer < iFramebufferCount; ++iFramebuffer)
	{
		mSecondaryBuffers[iFramebuffer] = gpCommandBufferManager->AllocateSecondaryBuffer(iFramebuffer, pcName);
	}
}

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

void GltfPipeline::RerecordSecondary(int64_t iFramebuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, const XMFLOAT4& rf4PushConstants)
{
	VkCommandBuffer vkSecondary = mSecondaryBuffers[iFramebuffer];

	VkCommandBufferInheritanceInfo vkInheritanceInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
		.pNext = nullptr,
		.renderPass = vkRenderPass,
		.subpass = 0,
		.framebuffer = vkFramebuffer,
		.occlusionQueryEnable = VK_FALSE,
		.queryFlags = 0,
		.pipelineStatistics = 0,
	};

	VkCommandBufferBeginInfo vkBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &vkInheritanceInfo,
	};

	CHECK_VK(vkBeginCommandBuffer(vkSecondary, &vkBeginInfo));
	RecordDrawIndirect(iFramebuffer, vkSecondary, rf4PushConstants);
	CHECK_VK(vkEndCommandBuffer(vkSecondary));
}

} // namespace engine
