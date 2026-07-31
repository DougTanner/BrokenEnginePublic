#if defined(BT_CLIENT)

#include "Pipeline.h"

#include "PipelineCreator.h"
#include "PipelineDescriptorWriter.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

namespace
{

// Always pushes the default 16-byte PushConstantsLayout range; callers guard that the pipeline uses the default range
// (iPushConstantBytes == 0), since a sub-16-byte override would overflow the layout's reserved push-constant range.
void RecordPushConstants(VkCommandBuffer vkCommandBuffer, VkPipelineLayout vkPipelineLayout, VkShaderStageFlags stageFlags, const XMFLOAT4& f4PushConstants)
{
	shaders::PushConstantsLayout pushConstantsLayout {};
	pushConstantsLayout.f4Pipeline = f4PushConstants;
	vkCmdPushConstants(vkCommandBuffer, vkPipelineLayout, stageFlags, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
}

void BindGraphicsDescriptorSets(VkCommandBuffer vkCommandBuffer, VkPipelineLayout vkPipelineLayout, VkDescriptorSetLayout vkExternalLayout, int64_t iDescriptorSetIndex, const std::vector<VkDescriptorSet>& rDescriptorSets)
{
	if (vkExternalLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iDescriptorSetIndex], rDescriptorSets[iDescriptorSetIndex]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipelineLayout, 0, 1, &rDescriptorSets[iDescriptorSetIndex], 0, nullptr);
	}
}

} // namespace

void BindComputeDescriptorSets(VkCommandBuffer vkCommandBuffer, VkPipelineLayout vkPipelineLayout, VkDescriptorSetLayout vkExternalLayout, int64_t iCommandBuffer, int64_t iDescriptorSetIndex, const std::vector<VkDescriptorSet>& rDescriptorSets)
{
	// Global Set 0 is indexed per-framebuffer (iCommandBuffer); per-pipeline Set 1 follows mbPerCommandBuffer (iDescriptorSetIndex).
	if (vkExternalLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iCommandBuffer], rDescriptorSets[iDescriptorSetIndex]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipelineLayout, 0, 1, &rDescriptorSets[iDescriptorSetIndex], 0, nullptr);
	}
}

uint32_t Pipeline::ResolveBindingSetIndex(const PipelineInfo& rPipelineInfo, uint32_t uiBinding)
{
	int64_t iBind = static_cast<int64_t>(uiBinding);
	const Shader* pFirstShader = rPipelineInfo.ppShaders[0];
	if (iBind < pFirstShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pFirstShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
	{
		return pFirstShader->mInfo.pDescriptorSetIndices[uiBinding];
	}
	if (rPipelineInfo.flags & kCompute)
	{
		return 0;
	}
	const Shader* pSecondShader = rPipelineInfo.ppShaders[1];
	if (iBind < pSecondShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pSecondShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
	{
		return pSecondShader->mInfo.pDescriptorSetIndices[uiBinding];
	}
	return 0;
}

Pipeline::Pipeline(const PipelineInfo& rInfo)
{
	Create(rInfo);
}

Pipeline::~Pipeline()
{
	Destroy();
}

void Pipeline::Create(const PipelineInfo& rInfo)
{
	Destroy();

	mbTexturesRequested = false;
	mTextureCrcs.clear();

	mInfo = rInfo;

	mVkExternalDescriptorSetLayout = mInfo.vkExternalDescriptorSetLayout != VK_NULL_HANDLE ? mInfo.vkExternalDescriptorSetLayout : gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout;
	mVkExternalDescriptorSetLayoutSet1 = mInfo.vkExternalDescriptorSetLayoutSet1;

	if (mInfo.flags & kRenderTarget)
	{
		ASSERT(mInfo.vkRenderPass != VK_NULL_HANDLE);
		ASSERT(mInfo.vkExtent3D.width != 0 && mInfo.vkExtent3D.height != 0);
	}

	mbPerCommandBuffer = mInfo.flags & kIndirectHostVisible;
	for (int64_t i = 0; i < static_cast<int64_t>(mInfo.pDescriptorInfos.size()); ++i)
	{
		if (mInfo.pDescriptorInfos[i].flags & kPerCommandBufferUniformBuffers || mInfo.pDescriptorInfos[i].flags & kPerCommandBufferStorageBuffers || mInfo.pDescriptorInfos[i].flags & kGlobalLayoutUniformBuffers || mInfo.pDescriptorInfos[i].flags & kMainLayoutUniformBuffers)
		{
			mbPerCommandBuffer = true;
		}
	}

	// The former fixed [kiMaxDescriptorSetLayoutBindings] array was the sole bound on the entry count, and
	// PipelineDescriptorWriter's stack scratch arrays are still sized to that cap. Bounds entries only — a
	// kModel entry expands into several descriptor writes, which the per-push cursor ASSERTs guard.
	ASSERT(static_cast<int64_t>(mInfo.pDescriptorInfos.size()) <= common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

	if (mInfo.flags & kCompute)
	{
		PipelineCreator::CreateComputePipeline(*this);
	}
	else
	{
		PipelineCreator::CreateGraphicsPipeline(*this);
	}

	PipelineDescriptorWriter::Write(*this);

	// Non-indirect pipelines (e.g. terrain, water) use RecordDraw and never call
	// WriteIndirectBuffer, so request their textures immediately
	if (!(mInfo.flags & kIndirectHostVisible) && !(mInfo.flags & kIndirectDeviceLocal) && !mTextureCrcs.empty())
	{
		mbTexturesRequested = true;
		gpFileManager->RequestChunkLoad(mTextureCrcs);
	}
}

void Pipeline::Destroy() noexcept
{
	if (mVkPipelineLayout == VK_NULL_HANDLE)
	{
		return;
	}

	gpTextureManager->mTextureDescriptors.UnregisterPipeline(this);

	VkDescriptorPool vkDescriptorPool = gpDeviceManager->mVkDescriptorPool;

	// Free Set 1 descriptor sets (not allocated for inner multi-set pipelines with external Set 1)
	if (!mVkDescriptorSets.empty())
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, vkDescriptorPool, static_cast<uint32_t>(mVkDescriptorSets.size()), mVkDescriptorSets.data());
	}
	mVkDescriptorSets.clear();

	// Free Set 2 descriptor sets
	if (!mVkDescriptorSetsSet2.empty())
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, vkDescriptorPool, static_cast<uint32_t>(mVkDescriptorSetsSet2.size()), mVkDescriptorSetsSet2.data());
	}
	mVkDescriptorSetsSet2.clear();

	vkDestroyPipeline(gpDeviceManager->mVkDevice, mVkPipeline, nullptr);
	mVkPipeline = VK_NULL_HANDLE;

	vkDestroyPipelineLayout(gpDeviceManager->mVkDevice, mVkPipelineLayout, nullptr);
	mVkPipelineLayout = VK_NULL_HANDLE;

	// Destroy Set 1 layout if we own it (not external from ModelPipeline or TextureManager)
	if (mVkDescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mVkDescriptorSetLayout, nullptr);
	}
	mVkDescriptorSetLayout = VK_NULL_HANDLE;

	// Destroy Set 2 layout
	if (mVkDescriptorSetLayoutSet2 != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mVkDescriptorSetLayoutSet2, nullptr);
		mVkDescriptorSetLayoutSet2 = VK_NULL_HANDLE;
	}

	if (mIndirectVkBuffer != VK_NULL_HANDLE)
	{
		if (mInfo.flags & kIndirectHostVisible)
		{
			mpIndirectMappedMemory = nullptr;
			mpIndirectComputeMappedMemory = nullptr;
		}

		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mIndirectVkBuffer, mIndirectVmaAllocation);
		mIndirectVkBuffer = VK_NULL_HANDLE;
		mIndirectVmaAllocation = VK_NULL_HANDLE;
	}

	// mModelMaterialsStorageBuffer is deliberately NOT torn down here — its member Buffer dtor owns the free.
	// The mDeviceLocalVkBuffer == VK_NULL_HANDLE guard in WriteModelDescriptor (PipelineDescriptorWriter.cpp)
	// exists to build the buffer once across Write()'s per-framebuffer loop, not to support in-place
	// re-Create() reuse: model pipelines are rebuild-only (fresh objects every time),
	// so a Destroy() on a pipeline holding this buffer is only ever followed by destruction. If an in-place
	// re-Create with a different scene ever appears, destroy the buffer here or the guard keeps stale materials.
}

void Pipeline::RecordDraw(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, int64_t iInstanceCount, int64_t iFirstInstance, const XMFLOAT4& f4PushConstants)
{
	ASSERT(!(mInfo.flags & kIndirectHostVisible) && !(mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, f4PushConstants);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	BindGraphicsDescriptorSets(vkCommandBuffer, mVkPipelineLayout, mVkExternalDescriptorSetLayout, iDescriptorSetIndex, mVkDescriptorSets);
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);

	vkCmdDrawIndexed(vkCommandBuffer, static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount), static_cast<uint32_t>(iInstanceCount), 0, 0, static_cast<uint32_t>(iFirstInstance));
}

void Pipeline::RecordBindPipelineAndDescriptors(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, f4PushConstants);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	BindGraphicsDescriptorSets(vkCommandBuffer, mVkPipelineLayout, mVkExternalDescriptorSetLayout, iDescriptorSetIndex, mVkDescriptorSets);
}

void Pipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, f4PushConstants);
	}

	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	BindGraphicsDescriptorSets(vkCommandBuffer, mVkPipelineLayout, mVkExternalDescriptorSetLayout, iCommandBuffer, mVkDescriptorSets);
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	// Device-local reads slot 0; host-visible indexes per-framebuffer
	int64_t iIndirectSlot = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer;
	VkDeviceSize vkIndirectOffset = iIndirectSlot * sizeof(VkDrawIndexedIndirectCommand);

	// Verify the indexed slot is within the indirect buffer's slot capacity
	ASSERT(iIndirectSlot < miIndirectSlotCount);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, vkIndirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordDrawIndirectSet2(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT(mInfo.flags & kMultiSet);
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, f4PushConstants);
	}

	// Bind pipeline and Set 2 only (Set 0, Set 1, and vertex buffer already bound by ModelPipeline)
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 2, 1, &mVkDescriptorSetsSet2[iCommandBuffer], 0, nullptr);
	// Device-local reads slot 0; host-visible indexes per-framebuffer
	int64_t iIndirectSlot = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer;
	VkDeviceSize vkIndirectOffset = iIndirectSlot * sizeof(VkDrawIndexedIndirectCommand);

	// Verify the indexed slot is within the indirect buffer's slot capacity
	ASSERT(iIndirectSlot < miIndirectSlotCount);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, vkIndirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordCompute(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ, const XMFLOAT4& f4PushConstants)
{
	ASSERT(!(mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, f4PushConstants);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	BindComputeDescriptorSets(vkCommandBuffer, mVkPipelineLayout, mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, mVkDescriptorSets);
	vkCmdDispatch(vkCommandBuffer, static_cast<uint32_t>(iGroupCountX), static_cast<uint32_t>(iGroupCountY), static_cast<uint32_t>(iGroupCountZ));
}

void Pipeline::RecordComputeIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		ASSERT(mInfo.iPushConstantBytes == 0);
		RecordPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, f4PushConstants);
	}

	// Host-visible indexes per-framebuffer; device-local reads slot 0
	int64_t iIndirectSlot = mInfo.flags & kIndirectHostVisible ? iCommandBuffer : 0;
	VkDeviceSize vkDispatchOffset = iIndirectSlot * sizeof(VkDispatchIndirectCommand);

	// Verify the indexed slot is within the indirect buffer's slot capacity
	ASSERT(iIndirectSlot < miIndirectSlotCount);

	RecordComputeIndirectFrom(iCommandBuffer, vkCommandBuffer, mIndirectVkBuffer, vkDispatchOffset);
}

void Pipeline::RecordComputeIndirectFrom(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, VkBuffer vkIndirectBuffer, VkDeviceSize vkIndirectOffset)
{
	ASSERT(mInfo.flags & kCompute);

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	BindComputeDescriptorSets(vkCommandBuffer, mVkPipelineLayout, mVkExternalDescriptorSetLayout, iCommandBuffer, iDescriptorSetIndex, mVkDescriptorSets);
	vkCmdDispatchIndirect(vkCommandBuffer, vkIndirectBuffer, vkIndirectOffset);
}

void Pipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iInstanceCount, int64_t iIndexCount, int64_t iFirstIndex, int64_t iVertexOffset)
{
	ASSERT(!(mInfo.flags & kCompute));

	// Request texture loading on first render with visible instances
	if (iInstanceCount > 0 && !mbTexturesRequested)
	{
		mbTexturesRequested = true;
		gpFileManager->RequestChunkLoad(mTextureCrcs);
	}

	ASSERT(mpIndirectMappedMemory != nullptr);

	VkDrawIndexedIndirectCommand& rCommand = mpIndirectMappedMemory[iCommandBuffer];

	// A negative iIndexCount means "draw the whole vertex buffer"; an explicit 0 stays a real zero-index draw, which is
	// how an empty material range is expressed. Never normalize one of the two into the other.
	rCommand.indexCount = static_cast<uint32_t>(iIndexCount < 0 ? mInfo.pVertexBuffer->mInfo.iCount : iIndexCount);
	rCommand.instanceCount = static_cast<uint32_t>(iInstanceCount);
	rCommand.firstIndex = static_cast<uint32_t>(iFirstIndex);
	rCommand.vertexOffset = static_cast<int32_t>(iVertexOffset);
	rCommand.firstInstance = 0;
}

void Pipeline::WriteIndirectComputeBuffer(int64_t iCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ)
{
	// Unlike WriteIndirectBuffer, this does not service the indirect demand-load deferral (Pipeline::Create
	// defers texture requests for indirect pipelines): a host-visible compute-indirect pipeline must bind
	// render-target textures only, never lazily disk-loaded ones.
	ASSERT((mInfo.flags & kIndirectHostVisible) && (mInfo.flags & kCompute));
	ASSERT(mpIndirectComputeMappedMemory != nullptr);

	VkDispatchIndirectCommand& rCommand = mpIndirectComputeMappedMemory[iCommandBuffer];
	rCommand.x = static_cast<uint32_t>(iGroupCountX);
	rCommand.y = static_cast<uint32_t>(iGroupCountY);
	rCommand.z = static_cast<uint32_t>(iGroupCountZ);
}

void Pipeline::UpdateStorageBufferDescriptor(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	PipelineDescriptorWriter::UpdateStorageBuffer(*this, iFramebuffer, iBinding, pBuffer);
}

void Pipeline::UpdateCombinedImageSamplerDescriptor(int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler)
{
	PipelineDescriptorWriter::UpdateCombinedImageSampler(*this, iBinding, vkImageView, vkSampler);
}

void Pipeline::UpdateSamplerDescriptor(int64_t iBinding, VkSampler vkSampler)
{
	PipelineDescriptorWriter::UpdateSampler(*this, iBinding, vkSampler);
}

void Pipeline::UpdateStorageImageDescriptor(int64_t iBinding, VkImageView vkImageView)
{
	PipelineDescriptorWriter::UpdateStorageImage(*this, iBinding, vkImageView);
}

} // namespace engine

#endif // defined(BT_CLIENT)
