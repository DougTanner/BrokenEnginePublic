#include "Pipeline.h"

#include "PipelineCreator.h"
#include "PipelineDescriptorWriter.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

Pipeline::Pipeline(const PipelineInfo& rInfo)
{
	Create(rInfo);
}

Pipeline::~Pipeline()
{
	Destroy();
}

void Pipeline::Create(const PipelineInfo& rInfo, bool bFromMultimaterial)
{
	Destroy();

	mbTexturesRequested = false;
	mTextureCrcs.clear();

	mInfo = rInfo;

	// All non-compute graphics pipelines use global descriptor Set 0 from TextureManager
	if (!(mInfo.flags & kCompute) && gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		mVkExternalDescriptorSetLayout = gpTextureManager->mTextureDescriptors.mGlobalDescriptorSetLayout;
	}

	// Add Model additional automatically
	constexpr int64_t kiModelAdditionalDescriptors = 5; // +1 lighting, +2 shadow, +3 smoke, +4 mesh data, +5 joint matrices
	for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings - kiModelAdditionalDescriptors; ++i)
	{
		const DescriptorInfo& rDescriptorInfo = mInfo.pDescriptorInfos[i];
		if (!(rDescriptorInfo.flags & DescriptorFlags::kModel))
		{
			continue;
		}

		ASSERT(i + kiModelAdditionalDescriptors + 1 < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
		ASSERT(mInfo.pDescriptorInfos[i + 1].flags == DescriptorFlags::kEmpty);

		mInfo.pDescriptorInfos[i + 1].flags = kCombinedSamplers;
		mInfo.pDescriptorInfos[i + 1].iCount = static_cast<int64_t>(std::size(gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures));
		mInfo.pDescriptorInfos[i + 1].ppTextures = gpTextureManager->mRenderTargetTextures.mppLightingFinalTextures;

		mInfo.pDescriptorInfos[i + 2].flags = kCombinedSamplers;
		mInfo.pDescriptorInfos[i + 2].iCount = 1;
		mInfo.pDescriptorInfos[i + 2].pTexture = &gpTextureManager->mRenderTargetTextures.mShadowBlurTexture;

		mInfo.pDescriptorInfos[i + 3].flags = {kCombinedSamplers, kSamplerBorder};
		mInfo.pDescriptorInfos[i + 3].iCount = 1;
		mInfo.pDescriptorInfos[i + 3].pTexture = &gpTextureManager->mRenderTargetTextures.mSmokeTextureOne;

		// Binding 15: Per-mesh data (matrix, normal matrix, joint count/offset)
		mInfo.pDescriptorInfos[i + 4].flags = kPerCommandBufferStorageBuffers;
		mInfo.pDescriptorInfos[i + 4].iExplicitBinding = 15;
		mInfo.pDescriptorInfos[i + 4].pBuffers = gpBufferManager->mMeshDataStorageBuffers.data();

		// Binding 16: Joint matrices (separate buffer to avoid NVIDIA driver hang)
		mInfo.pDescriptorInfos[i + 5].flags = kPerCommandBufferStorageBuffers;
		mInfo.pDescriptorInfos[i + 5].iExplicitBinding = 16;
		mInfo.pDescriptorInfos[i + 5].pBuffers = gpBufferManager->mJointMatrixStorageBuffers.data();
	}

	if (!bFromMultimaterial && mInfo.pDescriptorInfos[3].flags & kModel)
	{
		const EagerChunk& rChunk = gpFileManager->GetEagerChunkMap().at(mInfo.pDescriptorInfos[3].crc);
		ASSERT(rChunk.pHeader->sceneHeader.uiMaterialCount == 1);
	}

	if (mInfo.flags & kRenderTarget)
	{
		ASSERT(mInfo.vkRenderPass != VK_NULL_HANDLE);
		ASSERT(mInfo.vkExtent3D.width != 0 && mInfo.vkExtent3D.height != 0);
	}

	mbPerCommandBuffer = mInfo.flags & kIndirectHostVisible;
	for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
	{
		if (mInfo.pDescriptorInfos[i].flags & kPerCommandBufferUniformBuffers || mInfo.pDescriptorInfos[i].flags & kPerCommandBufferStorageBuffers)
		{
			mbPerCommandBuffer = true;
		}
	}

	if (mInfo.flags & kCompute)
	{
		PipelineCreator::CreateComputePipeline(*this, mInfo);
	}
	else
	{
		PipelineCreator::CreateGraphicsPipeline(*this, mInfo);
	}

	PipelineDescriptorWriter::Write(*this, mInfo);

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
		}

		vmaDestroyBuffer(gpDeviceManager->mpAllocator, mIndirectVkBuffer, mIndirectVmaAllocation);
		mIndirectVkBuffer = VK_NULL_HANDLE;
		mIndirectVkDeviceMemory = VK_NULL_HANDLE;
		mIndirectVmaAllocation = VK_NULL_HANDLE;
	}
}

void Pipeline::RecordDraw(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, int64_t iInstanceCount, int64_t iFirstInstance, const XMFLOAT4& f4PushConstants)
{
	ASSERT(!(mInfo.flags & kIndirectHostVisible) && !(mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	if (mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iDescriptorSetIndex], mVkDescriptorSets[iDescriptorSetIndex]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
	}
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);

	vkCmdDrawIndexed(vkCommandBuffer, static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount), static_cast<uint32_t>(iInstanceCount), 0, 0, static_cast<uint32_t>(iFirstInstance));
}

void Pipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	if (mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mTextureDescriptors.mGlobalDescriptorSets[iCommandBuffer], mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iCommandBuffer], 0, nullptr);
	}
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	VkDeviceSize vkIndirectOffset = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo vmaAllocationInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &vmaAllocationInfo);
	ASSERT(vkIndirectOffset + sizeof(VkDrawIndexedIndirectCommand) <= vmaAllocationInfo.size);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, vkIndirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordDrawIndirectSet2(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT(mInfo.flags & kMultiSet);
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	// Bind pipeline and Set 2 only (Set 0, Set 1, and vertex buffer already bound by ModelPipeline)
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 2, 1, &mVkDescriptorSetsSet2[iCommandBuffer], 0, nullptr);
	VkDeviceSize vkIndirectOffset = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);
	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, vkIndirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordCompute(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ, const XMFLOAT4& f4PushConstants)
{
	ASSERT(!(mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, 0);
	vkCmdDispatch(vkCommandBuffer, static_cast<uint32_t>(iGroupCountX), static_cast<uint32_t>(iGroupCountY), static_cast<uint32_t>(iGroupCountZ));
}

void Pipeline::RecordComputeIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, 0);
	VkDeviceSize vkDispatchOffset = mInfo.flags & kIndirectHostVisible ? iCommandBuffer * sizeof(VkDispatchIndirectCommand) : 0;

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo vmaAllocationInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &vmaAllocationInfo);
	ASSERT(vkDispatchOffset + sizeof(VkDispatchIndirectCommand) <= vmaAllocationInfo.size);

	vkCmdDispatchIndirect(vkCommandBuffer, mIndirectVkBuffer, vkDispatchOffset);
}

void Pipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iInstanceCount, int64_t iIndexCount, int64_t iFirstIndex)
{
	ASSERT(!(mInfo.flags & kCompute));

	// Request texture loading on first render with visible instances
	if (iInstanceCount > 0 && !mbTexturesRequested)
	{
		mbTexturesRequested = true;
		gpFileManager->RequestChunkLoad(mTextureCrcs);
	}

	if (mpIndirectMappedMemory == nullptr)
	{
		ASSERT(false);
		return;
	}

	VkDrawIndexedIndirectCommand& rCommand = mpIndirectMappedMemory[iCommandBuffer];

	rCommand.indexCount = static_cast<uint32_t>(iIndexCount == 0 ? static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount) : iIndexCount);
	rCommand.instanceCount = static_cast<uint32_t>(iInstanceCount);
	rCommand.firstIndex = static_cast<uint32_t>(iFirstIndex);
	rCommand.vertexOffset = 0;
	rCommand.firstInstance = 0;
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

} // namespace engine
