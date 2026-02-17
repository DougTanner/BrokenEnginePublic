#include "Pipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/DeviceManager.h"
#include "Graphics/Managers/ShaderManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

// Fallback for out-of-bounds binding lookups when vertex/fragment shaders have different binding counts
static constexpr VkDescriptorSetLayoutBinding kEmptyBinding {};

static VkDescriptorSetLayoutCreateInfo sUniformTextureVkDescriptorSetLayoutCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
	.pNext = nullptr,
	// .bindingCount
	// .pBindings
};

static VkPipelineLayoutCreateInfo sVkPipelineLayoutCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.setLayoutCount = 1,
	// .pSetLayouts
	// .pushConstantRangeCount
	// .pPushConstantRanges
};

static VkPipelineShaderStageCreateInfo spVkPipelineShaderStageCreateInfos[]
{
	VkPipelineShaderStageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		// .module
		.pName = "main",
		.pSpecializationInfo = nullptr,
	},
	VkPipelineShaderStageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = nullptr,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		// .module
		.pName = "main",
		.pSpecializationInfo = nullptr,
	},
};

static VkVertexInputBindingDescription sVkVertexInputBindingDescription
{
	.binding = 0,
	// .stride
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
};

static VkPipelineVertexInputStateCreateInfo sVkPipelineVertexInputStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = &sVkVertexInputBindingDescription,
	// .vertexAttributeDescriptionCount
	// .pVertexAttributeDescriptions
};

static VkPipelineInputAssemblyStateCreateInfo sVkPipelineInputAssemblyStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	.primitiveRestartEnable = VK_FALSE,
};

static VkViewport sVkViewport
{
	.x = 0.0f,
	.y = 0.0f,
	// .width
	// .height
	.minDepth = kfMinDepth,
	.maxDepth = kfMaxDepth,
};

static VkRect2D sScissorVkRect2D
{
	.offset = VkOffset2D {.x = 0, .y = 0},
	// .extent
};

static VkPipelineViewportStateCreateInfo sVkPipelineViewportStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.viewportCount = 1,
	.pViewports = &sVkViewport,
	.scissorCount = 1,
	.pScissors = &sScissorVkRect2D,
};

static VkPipelineRasterizationStateCreateInfo sVkPipelineRasterizationStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.depthClampEnable = VK_FALSE,
	.rasterizerDiscardEnable = VK_FALSE,
	// .polygonMode
	// .cullMode
	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // Note: this is different than the usual because we flip the Y co-ordinate in the perspective matrix
	// .depthBiasEnable
	// .depthBiasConstantFactor
	// .depthBiasClamp
	// .depthBiasSlopeFactor
	.lineWidth = 1.0f,
};

static VkPipelineMultisampleStateCreateInfo sVkPipelineMultisampleStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	// .rasterizationSamples
	// .sampleShadingEnable
	// .minSampleShading
	.pSampleMask = nullptr,
	.alphaToCoverageEnable = VK_FALSE,
	.alphaToOneEnable = VK_FALSE,
};

static VkPipelineDepthStencilStateCreateInfo sVkPipelineDepthStencilStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	// .depthTestEnable
	// .depthWriteEnable
	.depthCompareOp = VK_COMPARE_OP_LESS,
	.depthBoundsTestEnable = VK_FALSE,
	.stencilTestEnable = VK_FALSE,
	.front = {},
	.back = {},
	.minDepthBounds = 0.0f,
	.maxDepthBounds = 0.0f,
};

static VkPipelineColorBlendAttachmentState sVkPipelineColorBlendAttachmentState
{
	// .blendEnable
	// .srcColorBlendFactor
	// .dstColorBlendFactor
	// .colorBlendOp
	// .srcAlphaBlendFactor
	// .dstAlphaBlendFactor
	// .alphaBlendOp
	.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};

static VkPipelineColorBlendStateCreateInfo sVkPipelineColorBlendStateCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.logicOpEnable = VK_FALSE,
	.logicOp = VK_LOGIC_OP_COPY,
	.attachmentCount = 1,
	.pAttachments = &sVkPipelineColorBlendAttachmentState,
	.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
};

static VkGraphicsPipelineCreateInfo sVkGraphicsPipelineCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
	.pNext = nullptr,
	.flags = 0,
	.stageCount = 2,
	.pStages = spVkPipelineShaderStageCreateInfos,
	.pVertexInputState = &sVkPipelineVertexInputStateCreateInfo,
	.pInputAssemblyState = &sVkPipelineInputAssemblyStateCreateInfo,
	.pViewportState = &sVkPipelineViewportStateCreateInfo,
	.pRasterizationState = &sVkPipelineRasterizationStateCreateInfo,
	.pMultisampleState = &sVkPipelineMultisampleStateCreateInfo,
	.pDepthStencilState = &sVkPipelineDepthStencilStateCreateInfo,
	.pColorBlendState = &sVkPipelineColorBlendStateCreateInfo,
	.pDynamicState = nullptr,
	// .layout
	// .renderPass
	.basePipelineHandle = VK_NULL_HANDLE,
	.basePipelineIndex = -1,
};

// Configures update-after-bind for storage buffer bindings in dynamic pipelines
static void ConfigureUpdateAfterBind(const VkDescriptorSetLayoutBinding* pBindings, int64_t iDescriptorCount, VkDescriptorBindingFlags* pBindingFlags, VkDescriptorSetLayoutBindingFlagsCreateInfo& rBindingFlagsCreateInfo, bool bUpdateAfterBind)
{
	rBindingFlagsCreateInfo = VkDescriptorSetLayoutBindingFlagsCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.pNext = nullptr,
		.bindingCount = static_cast<uint32_t>(iDescriptorCount),
		.pBindingFlags = pBindingFlags,
	};

	if (bUpdateAfterBind)
	{
		for (int64_t i = 0; i < iDescriptorCount; ++i)
		{
			if (pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
				pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
				pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
			{
				pBindingFlags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
			}
		}
		sUniformTextureVkDescriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		sUniformTextureVkDescriptorSetLayoutCreateInfo.pNext = &rBindingFlagsCreateInfo;
	}
	else
	{
		sUniformTextureVkDescriptorSetLayoutCreateInfo.flags = 0;
		sUniformTextureVkDescriptorSetLayoutCreateInfo.pNext = nullptr;
	}
}

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
	if (!(mInfo.flags & kCompute) && gpTextureManager->mGlobalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		mVkExternalDescriptorSetLayout = gpTextureManager->mGlobalDescriptorSetLayout;
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
		mInfo.pDescriptorInfos[i + 1].iCount = static_cast<int64_t>(std::size(gpTextureManager->mppLightingFinalTextures));
		mInfo.pDescriptorInfos[i + 1].ppTextures = gpTextureManager->mppLightingFinalTextures;

		mInfo.pDescriptorInfos[i + 2].flags = kCombinedSamplers;
		mInfo.pDescriptorInfos[i + 2].iCount = 1;
		mInfo.pDescriptorInfos[i + 2].pTexture = &gpTextureManager->mShadowBlurTexture;

		mInfo.pDescriptorInfos[i + 3].flags = {kCombinedSamplers, kSamplerBorder};
		mInfo.pDescriptorInfos[i + 3].iCount = 1;
		mInfo.pDescriptorInfos[i + 3].pTexture = &gpTextureManager->mSmokeTextureOne;

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
		const EagerChunk& chunk = gpFileManager->GetEagerChunkMap().at(mInfo.pDescriptorInfos[3].crc);
		ASSERT(chunk.pHeader->sceneHeader.uiMaterialCount == 1);
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
		CreateComputePipeline(mInfo);
	}
	else
	{
		CreatePipeline(mInfo);
	}

	WriteDescriptorSets(mInfo);

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

	VkDescriptorPool pool = gpDeviceManager->mVkDescriptorPool;

	// Free Set 1 descriptor sets (not allocated for inner multi-set pipelines with external Set 1)
	if (!mVkDescriptorSets.empty())
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, pool, static_cast<uint32_t>(mVkDescriptorSets.size()), mVkDescriptorSets.data());
	}
	mVkDescriptorSets.clear();

	// Free Set 2 descriptor sets
	if (!mVkDescriptorSetsSet2.empty())
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, pool, static_cast<uint32_t>(mVkDescriptorSetsSet2.size()), mVkDescriptorSetsSet2.data());
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
		VkDescriptorSet sets[2] = {gpTextureManager->mGlobalDescriptorSets[iDescriptorSetIndex], mVkDescriptorSets[iDescriptorSetIndex]};
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
		VkDescriptorSet sets[2] = {gpTextureManager->mGlobalDescriptorSets[iCommandBuffer], mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iCommandBuffer], 0, nullptr);
	}
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	VkDeviceSize indirectOffset = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo allocInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &allocInfo);
	ASSERT(indirectOffset + sizeof(VkDrawIndexedIndirectCommand) <= allocInfo.size);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
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

	// Bind pipeline and Set 2 only (Set 0 + Set 1 already bound by ModelPipeline)
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 2, 1, &mVkDescriptorSetsSet2[iCommandBuffer], 0, nullptr);
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	VkDeviceSize indirectOffset = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);
	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordDrawIndirectWithAltDescriptorSet(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants, Pipeline& rAltPipeline)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, rAltPipeline.mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	// Use alternate pipeline's VkPipeline and descriptor sets, but our own indirect buffer
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipeline);
	if (rAltPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mGlobalDescriptorSets[iCommandBuffer], rAltPipeline.mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipelineLayout, 0, 1, &rAltPipeline.mVkDescriptorSets[iCommandBuffer], 0, nullptr);
	}
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	VkDeviceSize indirectOffset = mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo allocInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &allocInfo);
	ASSERT(indirectOffset + sizeof(VkDrawIndexedIndirectCommand) <= allocInfo.size);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordDrawIndirectWithAltEverything(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants, Pipeline& rAltPipeline)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && !(mInfo.flags & kCompute));

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, rAltPipeline.mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	// Use alternate pipeline's EVERYTHING - VkPipeline, descriptor sets, AND indirect buffer
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipeline);
	if (rAltPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		VkDescriptorSet sets[2] = {gpTextureManager->mGlobalDescriptorSets[iCommandBuffer], rAltPipeline.mVkDescriptorSets[iCommandBuffer]};
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipelineLayout, 0, 2, sets, 0, nullptr);
	}
	else
	{
		vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rAltPipeline.mVkPipelineLayout, 0, 1, &rAltPipeline.mVkDescriptorSets[iCommandBuffer], 0, nullptr);
	}
	rAltPipeline.mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	VkDeviceSize altIndirectOffset = rAltPipeline.mInfo.flags & kIndirectDeviceLocal ? 0 : iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand);

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo altAllocInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, rAltPipeline.mIndirectVmaAllocation, &altAllocInfo);
	ASSERT(altIndirectOffset + sizeof(VkDrawIndexedIndirectCommand) <= altAllocInfo.size);

	vkCmdDrawIndexedIndirect(vkCommandBuffer, rAltPipeline.mIndirectVkBuffer, altIndirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
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
	VkDeviceSize dispatchOffset = mInfo.flags & kIndirectHostVisible ? iCommandBuffer * sizeof(VkDispatchIndirectCommand) : 0;

	// Verify buffer is large enough for this command buffer index
	VmaAllocationInfo allocInfo {};
	vmaGetAllocationInfo(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &allocInfo);
	ASSERT(dispatchOffset + sizeof(VkDispatchIndirectCommand) <= allocInfo.size);

	vkCmdDispatchIndirect(vkCommandBuffer, mIndirectVkBuffer, dispatchOffset);
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

	VkDrawIndexedIndirectCommand& rCmd = mpIndirectMappedMemory[iCommandBuffer];

	rCmd.indexCount = static_cast<uint32_t>(iIndexCount == 0 ? static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount) : iIndexCount);
	rCmd.instanceCount = static_cast<uint32_t>(iInstanceCount);
	rCmd.firstIndex = static_cast<uint32_t>(iFirstIndex);
	rCmd.vertexOffset = 0;
	rCmd.firstInstance = 0;
}

void Pipeline::UpdateStorageBufferDescriptor(int64_t iFramebuffer, int64_t iBinding, Buffer* pBuffer)
{
	VkDescriptorBufferInfo vkDescriptorBufferInfo
	{
		.buffer = pBuffer->GetBuffer(),
		.offset = 0,
		.range = VK_WHOLE_SIZE,
	};

	VkWriteDescriptorSet vkWriteDescriptorSet
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = nullptr,
		.dstSet = mVkDescriptorSets.at(iFramebuffer),
		.dstBinding = static_cast<uint32_t>(iBinding),
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.pImageInfo = nullptr,
		.pBufferInfo = &vkDescriptorBufferInfo,
		.pTexelBufferView = nullptr,
	};

	vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
}

void Pipeline::UpdateCombinedImageSamplerDescriptor(int64_t iBinding, VkImageView vkImageView, VkSampler vkSampler)
{
	for (VkDescriptorSet& rVkDescriptorSet : mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = vkSampler,
			.imageView = vkImageView,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void Pipeline::UpdateSamplerDescriptor(int64_t iBinding, VkSampler vkSampler)
{
	for (VkDescriptorSet& rVkDescriptorSet : mVkDescriptorSets)
	{
		VkDescriptorImageInfo vkDescriptorImageInfo
		{
			.sampler = vkSampler,
			.imageView = nullptr,
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};

		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(iBinding),
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
			.pImageInfo = &vkDescriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void Pipeline::CreatePipeline(const PipelineInfo& rPipelineInfo)
{
	ASSERT(mInfo.name.size() > 0);

	if (mInfo.flags & kIndirectHostVisible)
	{
		// Use max of actual count and 3 to handle swapchain recreation scenarios
		size_t framebufferCount = gpSwapchainManager->mFramebuffers.size();
		int64_t iCommandBufferCount = std::max(framebufferCount, static_cast<size_t>(3));
		VkDeviceSize vkDeviceSize = iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand);
		VmaAllocationInfo vmaAllocationInfo {};
		Buffer::CreateBuffer(rPipelineInfo.name, vkDeviceSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory, mIndirectVmaAllocation, &vmaAllocationInfo);

		// Verify VMA gave us the memory properties we requested
		VkMemoryPropertyFlags memFlags = 0;
		vmaGetAllocationMemoryProperties(gpDeviceManager->mpAllocator, mIndirectVmaAllocation, &memFlags);
		ASSERT((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
		ASSERT((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);

		mpIndirectMappedMemory = static_cast<VkDrawIndexedIndirectCommand*>(vmaAllocationInfo.pMappedData);

		// Initialize all indirect buffer slots to zero
		for (int64_t i = 0; i < iCommandBufferCount; ++i)
		{
			VkDrawIndexedIndirectCommand& rCommand = mpIndirectMappedMemory[i];
			rCommand.indexCount = 0;
			rCommand.instanceCount = 0;
			rCommand.firstIndex = 0;
			rCommand.vertexOffset = 0;
			rCommand.firstInstance = 0;
		}
	}
	else if (mInfo.flags & kIndirectDeviceLocal)
	{
		// Use max of actual count and 3 to handle swapchain recreation scenarios
		size_t framebufferCount = gpSwapchainManager->mFramebuffers.size();
		int64_t iCommandBufferCount = std::max(framebufferCount, static_cast<size_t>(3));
		VkDeviceSize vkDeviceSize = iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand);
		Buffer::CreateBuffer(rPipelineInfo.name, vkDeviceSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory, mIndirectVmaAllocation);
	}

	Shader* pVertexShader = rPipelineInfo.ppShaders[0];
	Shader* pFragmentShader = rPipelineInfo.ppShaders[1];

	// Combine the descriptor set layouts from the vertex and fragment shaders
	// Filter out empty entries to avoid duplicate binding 0 errors from zero-initialized gaps
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iSourceCount = std::max(pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings, pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings);
	int64_t iDescriptorCount = 0;
	for (int64_t i = 0; i < iSourceCount; ++i)
	{
		const VkDescriptorSetLayoutBinding& vertBinding = i < pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pVertexShader->mInfo.pDescriptorBindings[i] : kEmptyBinding;
		const VkDescriptorSetLayoutBinding& fragBinding = i < pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pFragmentShader->mInfo.pDescriptorBindings[i] : kEmptyBinding;

		// Skip empty gap entries - they would all have binding=0 causing duplicates
		if (vertBinding.descriptorCount == 0 && fragBinding.descriptorCount == 0)
		{
			continue;
		}

		pVkDescriptorSetLayoutBindings[iDescriptorCount].binding = vertBinding.binding | fragBinding.binding;
		if (vertBinding.descriptorCount > 0 && fragBinding.descriptorCount > 0)
		{
			ASSERT(vertBinding.descriptorType == fragBinding.descriptorType);
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount].descriptorType = vertBinding.descriptorCount > 0 ? vertBinding.descriptorType : fragBinding.descriptorType;
		uint32_t uiDescriptorCount = std::max(vertBinding.descriptorCount, fragBinding.descriptorCount);
		// Runtime-sized arrays exported with UINT32_MAX sentinel; replace with actual texture array size
		if (uiDescriptorCount == UINT32_MAX)
		{
			uiDescriptorCount = static_cast<uint32_t>(gpTextureManager->mImageInfos.size());
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount].descriptorCount = uiDescriptorCount;
		pVkDescriptorSetLayoutBindings[iDescriptorCount].stageFlags = vertBinding.stageFlags | fragBinding.stageFlags;
		pVkDescriptorSetLayoutBindings[iDescriptorCount].pImmutableSamplers = vertBinding.pImmutableSamplers != nullptr ? vertBinding.pImmutableSamplers : fragBinding.pImmutableSamplers;
		++iDescriptorCount;
	}
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = mInfo.uiPushConstantSize;

	if (mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		// Graphics pipeline with global Set 0: split bindings by shader reflection set index
		VkDescriptorSetLayoutBinding pVkSet1Bindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBinding pVkSet2Bindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		int64_t iSet1Count = 0;
		int64_t iSet2Count = 0;

		for (int64_t i = 0; i < iDescriptorCount; ++i)
		{
			uint32_t uiBinding = pVkDescriptorSetLayoutBindings[i].binding;
			uint32_t uiSet = 0;
			if (uiBinding < static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings) && pVertexShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
			{
				uiSet = pVertexShader->mInfo.pDescriptorSetIndices[uiBinding];
			}
			else if (uiBinding < static_cast<uint32_t>(pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings) && pFragmentShader->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
			{
				uiSet = pFragmentShader->mInfo.pDescriptorSetIndices[uiBinding];
			}

			if (uiSet == 1)
			{
				pVkSet1Bindings[iSet1Count++] = pVkDescriptorSetLayoutBindings[i];
			}
			else if (uiSet == 2)
			{
				pVkSet2Bindings[iSet2Count++] = pVkDescriptorSetLayoutBindings[i];
			}
			// Set 0 bindings are handled by global descriptor set
		}

		// Create Set 1 layout (unless using external layout from first ModelPipeline material)
		if (mVkExternalDescriptorSetLayoutSet1 == VK_NULL_HANDLE)
		{
			sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet1Count);
			sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkSet1Bindings;
			VkDescriptorBindingFlags pBindingFlagsSet1[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet1 {};
			ConfigureUpdateAfterBind(pVkSet1Bindings, iSet1Count, pBindingFlagsSet1, bindingFlagsCreateInfoSet1, mInfo.flags & kUpdateAfterBind);
			CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayout));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mVkDescriptorSetLayout, mInfo.name.data());
		}

		// Create Set 2 layout (kMultiSet models only)
		if (mInfo.flags & kMultiSet)
		{
			sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet2Count);
			sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkSet2Bindings;
			VkDescriptorBindingFlags pBindingFlagsSet2[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet2 {};
			ConfigureUpdateAfterBind(pVkSet2Bindings, iSet2Count, pBindingFlagsSet2, bindingFlagsCreateInfoSet2, mInfo.flags & kUpdateAfterBind);
			CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayoutSet2));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mVkDescriptorSetLayoutSet2, mInfo.name.data());
		}

		// Pipeline layout: [global Set 0, Set 1, optional Set 2]
		VkDescriptorSetLayout pSetLayouts[3] =
		{
			mVkExternalDescriptorSetLayout,
			mVkExternalDescriptorSetLayoutSet1 != VK_NULL_HANDLE ? mVkExternalDescriptorSetLayoutSet1 : mVkDescriptorSetLayout,
			mVkDescriptorSetLayoutSet2,
		};
		uint32_t uiSetCount = (mInfo.flags & kMultiSet) ? 3 : 2;
		sVkPipelineLayoutCreateInfo.setLayoutCount = uiSetCount;
		sVkPipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
		sVkPipelineLayoutCreateInfo.pushConstantRangeCount = mInfo.flags & kPushConstants ? 1 : 0;
		sVkPipelineLayoutCreateInfo.pPushConstantRanges = mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
		CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &sVkPipelineLayoutCreateInfo, nullptr, &mVkPipelineLayout));
		VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, mVkPipelineLayout, mInfo.name.data());

		// Reset static for next pipeline creation
		sVkPipelineLayoutCreateInfo.setLayoutCount = 1;
	}
	else
	{
		// Compute pipelines or pipelines without global Set 0: single descriptor set
		sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
		sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkDescriptorSetLayoutBindings;

		VkDescriptorBindingFlags pBindingFlags[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo {};
		ConfigureUpdateAfterBind(pVkDescriptorSetLayoutBindings, iDescriptorCount, pBindingFlags, bindingFlagsCreateInfo, mInfo.flags & kUpdateAfterBind);

		CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayout));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mVkDescriptorSetLayout, mInfo.name.data());

		sVkPipelineLayoutCreateInfo.pSetLayouts = &mVkDescriptorSetLayout;
		sVkPipelineLayoutCreateInfo.pushConstantRangeCount = mInfo.flags & kPushConstants ? 1 : 0;
		sVkPipelineLayoutCreateInfo.pPushConstantRanges = mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
		CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &sVkPipelineLayoutCreateInfo, nullptr, &mVkPipelineLayout));
		VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, mVkPipelineLayout, mInfo.name.data());
	}

	// Setup pipeline
	spVkPipelineShaderStageCreateInfos[0].module = pVertexShader->mVkShaderModule;
	spVkPipelineShaderStageCreateInfos[1].module = pFragmentShader->mVkShaderModule;

	ASSERT(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride == rPipelineInfo.pVertexBuffer->mInfo.iVertexStride);
	sVkVertexInputBindingDescription.stride = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride);

	sVkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputAttributeDescriptions);
	sVkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = pVertexShader->mInfo.pVertexAttributes;

	VkExtent2D vkExtent2D
	{
		.width = mInfo.flags & kRenderTarget ? mInfo.vkExtent3D.width : gpGraphics->mFramebufferExtent2D.width,
		.height = mInfo.flags & kRenderTarget ? mInfo.vkExtent3D.height : gpGraphics->mFramebufferExtent2D.height,
	};

	// Negative viewport height (VK_KHR_maintenance1) flips the Vulkan Y axis to match DirectX convention
	sVkViewport.x = 0.0f;
	sVkViewport.y = static_cast<float>(vkExtent2D.height);
	sVkViewport.width = static_cast<float>(vkExtent2D.width);
	sVkViewport.height = -static_cast<float>(vkExtent2D.height);

	sScissorVkRect2D.extent = vkExtent2D;

	sVkPipelineColorBlendAttachmentState.blendEnable = (rPipelineInfo.flags & kAlphaBlend || rPipelineInfo.flags & kAdd || rPipelineInfo.flags & kAddAlpha || rPipelineInfo.flags & kMax) ? VK_TRUE : VK_FALSE;
	sVkPipelineColorBlendAttachmentState.colorBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	sVkPipelineColorBlendAttachmentState.alphaBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	if (rPipelineInfo.flags & kAlphaBlend)
	{
		sVkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		sVkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		sVkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	}
	else if (rPipelineInfo.flags & kAddAlpha)
	{
		sVkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		sVkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		sVkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else
	{
		sVkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}

	if constexpr (kbEnableWireframe)
	{
		bool bWireframe = gWireframe.Get<bool>();
		if (mInfo.flags & kRenderTarget || mInfo.flags & kNoWireframe)
		{
			bWireframe = false;
		}
		sVkPipelineRasterizationStateCreateInfo.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	}
	else
	{
		sVkPipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	}
	sVkPipelineRasterizationStateCreateInfo.cullMode = rPipelineInfo.flags & kCullBack ? VK_CULL_MODE_BACK_BIT : (rPipelineInfo.flags & kCullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE);
	if (rPipelineInfo.flags & kDepthBias)
	{
		sVkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_TRUE;
		sVkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = -3.0f;
		sVkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		sVkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = -3.0f;
	}
	else
	{
		sVkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		sVkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
		sVkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		sVkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
	}

	sVkPipelineDepthStencilStateCreateInfo.depthTestEnable = rPipelineInfo.flags & kDepthTest ? VK_TRUE : VK_FALSE;
	sVkPipelineDepthStencilStateCreateInfo.depthWriteEnable = rPipelineInfo.flags & kDepthWrite ? VK_TRUE : VK_FALSE;

	sVkPipelineMultisampleStateCreateInfo.rasterizationSamples = mInfo.flags & kRenderTarget ? VK_SAMPLE_COUNT_1_BIT : (gMultisampling.Get<bool>() ? gSampleCount.Get<VkSampleCountFlagBits>() : VK_SAMPLE_COUNT_1_BIT);
	sVkPipelineMultisampleStateCreateInfo.sampleShadingEnable = (rPipelineInfo.flags & kSampleShading && gSampleShading.Get<bool>()) ? VK_TRUE : VK_FALSE;
	sVkPipelineMultisampleStateCreateInfo.minSampleShading = gMinSampleShading.Get();

	sVkGraphicsPipelineCreateInfo.layout = mVkPipelineLayout;
	sVkGraphicsPipelineCreateInfo.renderPass = mInfo.flags & kRenderTarget ? rPipelineInfo.vkRenderPass : gpSwapchainManager->mVkRenderPass;

	// Configure MRT blend states for lighting pass
	VkPipelineColorBlendAttachmentState pMrtBlendStates[3] = {};
	if (rPipelineInfo.vkRenderPass == gpTextureManager->mLightingVkRenderPass)
	{
		for (int64_t i = 0; i < 3; ++i)
		{
			pMrtBlendStates[i] = sVkPipelineColorBlendAttachmentState;
		}
		sVkPipelineColorBlendStateCreateInfo.attachmentCount = 3;
		sVkPipelineColorBlendStateCreateInfo.pAttachments = pMrtBlendStates;
	}
	else
	{
		sVkPipelineColorBlendStateCreateInfo.attachmentCount = 1;
		sVkPipelineColorBlendStateCreateInfo.pAttachments = &sVkPipelineColorBlendAttachmentState;
	}

	CHECK_VK(vkCreateGraphicsPipelines(gpDeviceManager->mVkDevice, VK_NULL_HANDLE, 1, &sVkGraphicsPipelineCreateInfo, nullptr, &mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, mVkPipeline, mInfo.name.data());
}

void Pipeline::CreateComputePipeline(const PipelineInfo& rPipelineInfo)
{
	if (mInfo.flags & kIndirectHostVisible)
	{
		ASSERT(false);
	}
	else if (mInfo.flags & kIndirectDeviceLocal)
	{
		Buffer::CreateBuffer(rPipelineInfo.name, sizeof(VkDispatchIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory, mIndirectVmaAllocation);
	}

	Shader* pComputeShader = rPipelineInfo.ppShaders[0];

	// Filter out empty entries to avoid duplicate binding 0 errors from zero-initialized gaps
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iSourceCount = pComputeShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings;
	int64_t iDescriptorCount = 0;
	for (int64_t i = 0; i < iSourceCount; ++i)
	{
		const VkDescriptorSetLayoutBinding& binding = pComputeShader->mInfo.pDescriptorBindings[i];
		if (binding.descriptorCount == 0)
		{
			continue;
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount++] = binding;
	}
	sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
	sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkDescriptorSetLayoutBindings;

	VkDescriptorBindingFlags pBindingFlags[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo {};
	ConfigureUpdateAfterBind(pVkDescriptorSetLayoutBindings, iDescriptorCount, pBindingFlags, bindingFlagsCreateInfo, mInfo.flags & kUpdateAfterBind);

	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayout));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mVkDescriptorSetLayout, mInfo.name.data());

	sVkPipelineLayoutCreateInfo.pSetLayouts = &mVkDescriptorSetLayout;
	sVkPipelineLayoutCreateInfo.pushConstantRangeCount = mInfo.flags & kPushConstants ? 1 : 0;
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = mInfo.uiPushConstantSize;
	sVkPipelineLayoutCreateInfo.pPushConstantRanges = mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &sVkPipelineLayoutCreateInfo, nullptr, &mVkPipelineLayout));
	VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, mVkPipelineLayout, mInfo.name.data());

	VkComputePipelineCreateInfo vkComputePipelineCreateInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	vkComputePipelineCreateInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vkComputePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	vkComputePipelineCreateInfo.stage.module = pComputeShader->mVkShaderModule;
	vkComputePipelineCreateInfo.stage.pName = "main";
	vkComputePipelineCreateInfo.layout = mVkPipelineLayout;
	CHECK_VK(vkCreateComputePipelines(gpDeviceManager->mVkDevice, VK_NULL_HANDLE, 1, &vkComputePipelineCreateInfo, nullptr, &mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, mVkPipeline, mInfo.name.data());
}

void Pipeline::WriteDescriptorSets(const PipelineInfo& rPipelineInfo)
{
	// Check if a binding exists in the shader layout (prevents registering deferred texture updates for bindings that don't exist, e.g. shadow pipelines)
	auto bindingExistsInShaderLayout = [&rPipelineInfo, this](uint32_t uiBinding) -> bool
	{
		if (mInfo.flags & kCompute)
		{
			return true;
		}
		if (uiBinding >= common::ShaderHeader::kiMaxDescriptorSetLayoutBindings)
		{
			return false;
		}
		int64_t iBind = static_cast<int64_t>(uiBinding);
		const VkDescriptorSetLayoutBinding& rVertBinding = iBind < rPipelineInfo.ppShaders[0]->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rPipelineInfo.ppShaders[0]->mInfo.pDescriptorBindings[uiBinding] : kEmptyBinding;
		const VkDescriptorSetLayoutBinding& rFragBinding = iBind < rPipelineInfo.ppShaders[1]->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rPipelineInfo.ppShaders[1]->mInfo.pDescriptorBindings[uiBinding] : kEmptyBinding;
		return rVertBinding.descriptorCount > 0 || rFragBinding.descriptorCount > 0;
	};

	// Check if a binding belongs to global Set 0 (must not register for per-pipeline descriptor updates)
	auto bindingIsInSet0 = [&rPipelineInfo, this](uint32_t uiBinding) -> bool
	{
		if (mVkExternalDescriptorSetLayout == VK_NULL_HANDLE)
		{
			return false;
		}
		int64_t iBind = static_cast<int64_t>(uiBinding);
		const Shader* pVert = rPipelineInfo.ppShaders[0];
		const Shader* pFrag = rPipelineInfo.ppShaders[1];
		if (iBind < pVert->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pVert->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
		{
			return pVert->mInfo.pDescriptorSetIndices[uiBinding] == 0;
		}
		if (iBind < pFrag->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pFrag->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
		{
			return pFrag->mInfo.pDescriptorSetIndices[uiBinding] == 0;
		}
		return false;
	};

	bool bMultiSet = mInfo.flags & kMultiSet;
	bool bHasExternalSet0 = mVkExternalDescriptorSetLayout != VK_NULL_HANDLE;
	bool bHasExternalSet1 = mVkExternalDescriptorSetLayoutSet1 != VK_NULL_HANDLE;

	int64_t iPerCommandBuffer = mbPerCommandBuffer ? gpSwapchainManager->mFramebuffers.size() : 1;
	if (!bHasExternalSet1)
	{
		mVkDescriptorSets.resize(iPerCommandBuffer);
	}
	if (bMultiSet)
	{
		mVkDescriptorSetsSet2.resize(iPerCommandBuffer);
	}

	for (int64_t iFramebuffer = 0; iFramebuffer < iPerCommandBuffer; ++iFramebuffer)
	{
		VkDescriptorPool vkPool = gpDeviceManager->mVkDescriptorPool;

		// Allocate Set 1 descriptor set (or single set for compute pipelines)
		VkDescriptorSet rVkDescriptorSet = VK_NULL_HANDLE;
		if (!bHasExternalSet1)
		{
			VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfo
			{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = vkPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &mVkDescriptorSetLayout,
			};
			CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &vkDescriptorSetAllocateInfo, &mVkDescriptorSets.at(iFramebuffer)));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, mVkDescriptorSets.at(iFramebuffer), std::format("{}{}", mInfo.name.data(), iFramebuffer).c_str());
			rVkDescriptorSet = mVkDescriptorSets.at(iFramebuffer);
		}

		// Allocate Set 2 descriptor set (multi-set models only)
		VkDescriptorSet vkDstSetSet2 = VK_NULL_HANDLE;
		if (bMultiSet)
		{
			VkDescriptorSetAllocateInfo set2AllocInfo
			{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.pNext = nullptr,
				.descriptorPool = vkPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &mVkDescriptorSetLayoutSet2,
			};
			CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &set2AllocInfo, &mVkDescriptorSetsSet2.at(iFramebuffer)));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, mVkDescriptorSetsSet2.at(iFramebuffer), std::format("{}Set2{}", mInfo.name.data(), iFramebuffer).c_str());
			vkDstSetSet2 = mVkDescriptorSetsSet2.at(iFramebuffer);
		}

		int64_t iDescriptorCount = 0;
		VkWriteDescriptorSet pVkWriteDescriptorSets[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		constexpr int64_t kiMaxImageInfos = 128;
		int64_t iImageInfoCount = 0;
		VkDescriptorImageInfo pVkDescriptorImageInfos[kiMaxImageInfos] {};
		int64_t iBufferInfoCount = 0;
		VkDescriptorBufferInfo pVkDescriptorBufferInfos[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
		{
			const DescriptorInfo& rDescriptorInfo = rPipelineInfo.pDescriptorInfos[i];
			if (rDescriptorInfo.flags & kEmpty)
			{
				break;
			}

			// Use explicit binding if specified, otherwise use sequential counter
			uint32_t uiBinding = rDescriptorInfo.iExplicitBinding >= 0 ? static_cast<uint32_t>(rDescriptorInfo.iExplicitBinding) : static_cast<uint32_t>(iDescriptorCount);

			VkWriteDescriptorSet vkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = rVkDescriptorSet,
				.dstBinding = uiBinding,
				.dstArrayElement = 0,
				// .descriptorCount
				// .descriptorType
				// .pImageInfo
				// .pBufferInfo
				.pTexelBufferView = nullptr
			};

			bool bSampler = rDescriptorInfo.flags & kSamplerClamp || rDescriptorInfo.flags & kSamplerBorder || rDescriptorInfo.flags & kSamplerRepeat || rDescriptorInfo.flags & kSamplerMirroredRepeat || rDescriptorInfo.flags & kSamplerSmoke || rDescriptorInfo.flags & kSamplerWindClamp;
			if (rDescriptorInfo.flags & kModel)
			{
				const EagerChunk& chunk = gpFileManager->GetEagerChunkMap().at(rDescriptorInfo.crc);

				// Sampler for bindless texture array
				{
					VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
					ASSERT(iImageInfoCount < kiMaxImageInfos);
					rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
					rVkDescriptorImageInfo.imageView = nullptr;
					rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
					vkWriteDescriptorSet.descriptorCount = 1;
					vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
					vkWriteDescriptorSet.pBufferInfo = nullptr;

					pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
					ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

					if (iFramebuffer == 0 && bindingExistsInShaderLayout(static_cast<uint32_t>(iDescriptorCount - 1)) && !bindingIsInSet0(static_cast<uint32_t>(iDescriptorCount - 1)))
					{
						gpTextureManager->RegisterStandaloneSamplerBinding(this, iDescriptorCount - 1, kSamplerRepeat);
					}
				}

				// Bindless texture array
				{
					vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
					vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mImageInfos.size());
					vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					vkWriteDescriptorSet.pImageInfo = gpTextureManager->mImageInfos.data();
					vkWriteDescriptorSet.pBufferInfo = nullptr;

					pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
					ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				}

				// Irradiance
				Texture& rIrradianceTexture = gpTextureManager->mTextureMap.at(data::kTexturesCKloofendalPuresky_IrradianceR16G16B16A16_SFLOATCrc);
				VkDescriptorImageInfo& rVkDescriptorImageInfoIrradiance = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoIrradiance.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoIrradiance.imageView = rIrradianceTexture.mVkImageView;
				rVkDescriptorImageInfoIrradiance.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoIrradiance;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				if (iFramebuffer == 0 && bindingExistsInShaderLayout(static_cast<uint32_t>(iDescriptorCount - 1)) && !bindingIsInSet0(static_cast<uint32_t>(iDescriptorCount - 1)))
				{
					gpTextureManager->RegisterTextureBinding(data::kTexturesCKloofendalPuresky_IrradianceR16G16B16A16_SFLOATCrc, this, iDescriptorCount - 1, kSamplerRepeat, &rIrradianceTexture);
				}

				// PreFiltered
				Texture& rPreFilteredTexture = gpTextureManager->mTextureMap.at(data::kTexturesCKloofendalPuresky_PrefilteredR16G16B16A16_SFLOATCrc);
				VkDescriptorImageInfo& rVkDescriptorImageInfoPreFiltered = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoPreFiltered.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoPreFiltered.imageView = rPreFilteredTexture.mVkImageView;
				rVkDescriptorImageInfoPreFiltered.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoPreFiltered;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				if (iFramebuffer == 0 && bindingExistsInShaderLayout(static_cast<uint32_t>(iDescriptorCount - 1)) && !bindingIsInSet0(static_cast<uint32_t>(iDescriptorCount - 1)))
				{
					gpTextureManager->RegisterTextureBinding(data::kTexturesCKloofendalPuresky_PrefilteredR16G16B16A16_SFLOATCrc, this, iDescriptorCount - 1, kSamplerRepeat, &rPreFilteredTexture);
				}

				// LutBrdf
				VkDescriptorImageInfo& rVkDescriptorImageInfoLutBrdf = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoLutBrdf.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoLutBrdf.imageView = gpTextureManager->mPbrLutBrdfTexture.mVkImageView;
				rVkDescriptorImageInfoLutBrdf.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoLutBrdf;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				if (iFramebuffer == 0 && bindingExistsInShaderLayout(static_cast<uint32_t>(iDescriptorCount - 1)) && !bindingIsInSet0(static_cast<uint32_t>(iDescriptorCount - 1)))
				{
					gpTextureManager->RegisterTextureBinding(0, this, iDescriptorCount - 1, kSamplerRepeat, &gpTextureManager->mPbrLutBrdfTexture);
				}

				// Materials
				if (mModelMaterialsStorageBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE)
				{
					mModelMaterialsStorageBuffer.Create(
					{
						.name = "Materials",
						.flags = {BufferFlags::kStorage, BufferFlags::kDeviceLocal},
						.dataVkDeviceSize = chunk.pHeader->sceneHeader.uiMaterialCount * sizeof(shaders::PbrMaterialLayout),
					},
					[&](void* pData)
					{
						const common::SceneHeader& rSH = chunk.pHeader->sceneHeader;
						int64_t iArraysSize = common::RoundUp<int64_t, common::kiAlignmentBytes>(rSH.uiTextureCount * static_cast<int64_t>(sizeof(common::crc_t)))
						                    + common::RoundUp<int64_t, common::kiAlignmentBytes>(rSH.uiMaterialCount * static_cast<int64_t>(sizeof(uint32_t)));
						const common::crc_t* pTexCrcs = reinterpret_cast<const common::crc_t*>(chunk.pData);
						const common::MaterialShaderData* pMaterialShaderData = reinterpret_cast<const common::MaterialShaderData*>(chunk.pData + iArraysSize);
						shaders::PbrMaterialLayout* pCurrent = static_cast<shaders::PbrMaterialLayout*>(pData);
						constexpr int64_t kiOldMaterialSize = offsetof(shaders::PbrMaterialLayout, fColorTextureIndex);
						for (int64_t j = 0; j < rSH.uiMaterialCount; ++j)
						{
							memcpy(pCurrent, &pMaterialShaderData[j].f4BaseColorFactor, kiOldMaterialSize);
							pCurrent->fColorTextureIndex = gpTextureManager->CrcToIndex(pTexCrcs[pMaterialShaderData[j].uiColorTextureIndex]);
							pCurrent->fPhysicalDescriptorTextureIndex = gpTextureManager->CrcToIndex(pTexCrcs[pMaterialShaderData[j].uiPhysicalDescriptorTextureIndex]);
							pCurrent->fNormalTextureIndex = gpTextureManager->CrcToIndex(pTexCrcs[pMaterialShaderData[j].uiNormalTextureIndex]);
							pCurrent->fOcclusionTextureIndex = gpTextureManager->CrcToIndex(pTexCrcs[pMaterialShaderData[j].uiOcclusionTextureIndex]);
							pCurrent->fEmissiveTextureIndex = gpTextureManager->CrcToIndex(pTexCrcs[pMaterialShaderData[j].uiEmissiveTextureIndex]);
							pCurrent++;
						}
					});
				}

				VkDescriptorBufferInfo& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[iBufferInfoCount++];
				ASSERT(iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				rVkDescriptorBufferInfo.buffer = mModelMaterialsStorageBuffer.mDeviceLocalVkBuffer;
				rVkDescriptorBufferInfo.offset = 0;
				rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				vkWriteDescriptorSet.pImageInfo = nullptr;
				vkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;
			}
			else if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers)
			{
				VkBuffer vkBuffer = VK_NULL_HANDLE;
				if (rDescriptorInfo.flags & kUniformBuffer || rDescriptorInfo.flags & kStorageBuffer)
				{
					vkBuffer = rDescriptorInfo.pBuffers != nullptr ? rDescriptorInfo.pBuffers->GetBuffer() : *rDescriptorInfo.pVkBuffers;
				}
				else if (rDescriptorInfo.flags & kPerCommandBufferUniformBuffers || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers)
				{
					vkBuffer = rDescriptorInfo.pBuffers[iFramebuffer].GetBuffer();
				}
				ASSERT(vkBuffer != VK_NULL_HANDLE);

				VkDescriptorBufferInfo& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[iBufferInfoCount++];
				ASSERT(iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				rVkDescriptorBufferInfo.buffer = vkBuffer;
				rVkDescriptorBufferInfo.offset = 0;
				rVkDescriptorBufferInfo.range = VK_WHOLE_SIZE;

				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kStorageBuffer || rDescriptorInfo.flags & kPerCommandBufferStorageBuffers ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				vkWriteDescriptorSet.pImageInfo = nullptr;
				vkWriteDescriptorSet.pBufferInfo = &rVkDescriptorBufferInfo;
			}
			else if (bSampler && !(rDescriptorInfo.flags & kCombinedSamplers))
			{
				VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(rDescriptorInfo.flags);
				rVkDescriptorImageInfo.imageView = nullptr;
				rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				if (iFramebuffer == 0 && bindingExistsInShaderLayout(uiBinding) && !bindingIsInSet0(uiBinding))
				{
					gpTextureManager->RegisterStandaloneSamplerBinding(this, iDescriptorCount, rDescriptorInfo.flags);
				}
			}
			else if (rDescriptorInfo.flags & kTextures)
			{
				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mImageInfos.size());
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				vkWriteDescriptorSet.pImageInfo = gpTextureManager->mImageInfos.data();
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else if (rDescriptorInfo.flags & kCombinedSamplers || rDescriptorInfo.flags & kStorageImages)
			{
				VkDescriptorImageInfo* pStart = &pVkDescriptorImageInfos[iImageInfoCount];

				for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
				{
					VkDescriptorImageInfo& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
					ASSERT(iImageInfoCount < kiMaxImageInfos);
					rVkDescriptorImageInfo.sampler = rDescriptorInfo.flags & kCombinedSamplers ? gpTextureManager->GetSampler(rDescriptorInfo.flags) : nullptr;

					if (rDescriptorInfo.textureCrc != 0)
					{
						rVkDescriptorImageInfo.imageView = gpTextureManager->mTextureMap.at(rDescriptorInfo.textureCrc).mVkImageView;
					}
					else if (rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr)
					{
						// Runtime-only texture (e.g. render targets, generated textures). Data-packed textures must use
						// the textureCrc path instead so they get deferred descriptor updates when lazy-loaded.
						ASSERT(rDescriptorInfo.pTexture->mInfo.crc == 0);
						rVkDescriptorImageInfo.imageView = rDescriptorInfo.pTexture->mVkImageView;
					}
					else
					{
						// Array of texture pointers
						rVkDescriptorImageInfo.imageView = rDescriptorInfo.ppTextures[k]->mVkImageView;
					}

					rVkDescriptorImageInfo.imageLayout = rDescriptorInfo.flags & kCombinedSamplers ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
				}

				// Register combined image sampler bindings for deferred texture and sampler descriptor updates
				if (iFramebuffer == 0 && rDescriptorInfo.flags & kCombinedSamplers && bindingExistsInShaderLayout(uiBinding) && !bindingIsInSet0(uiBinding))
				{
					if (rDescriptorInfo.textureCrc != 0)
					{
						Texture* pTexture = &gpTextureManager->mTextureMap.at(rDescriptorInfo.textureCrc);
						gpTextureManager->RegisterTextureBinding(rDescriptorInfo.textureCrc, this, iDescriptorCount, rDescriptorInfo.flags, pTexture);
						mTextureCrcs.push_back(rDescriptorInfo.textureCrc);
					}
					else if (rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr)
					{
						gpTextureManager->RegisterTextureBinding(0, this, iDescriptorCount, rDescriptorInfo.flags, rDescriptorInfo.pTexture);
					}
					else if (rDescriptorInfo.ppTextures != nullptr)
					{
						// Register per-CRC entries for lazy texture loading and sampler updates
						for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
						{
							common::crc_t arrayCrc = rDescriptorInfo.ppTextures[k]->mInfo.crc;
							if (arrayCrc != 0 && gpTextureManager->mTextureMap.contains(arrayCrc))
							{
								gpTextureManager->RegisterTextureBinding(arrayCrc, this, iDescriptorCount, rDescriptorInfo.flags, nullptr, rDescriptorInfo.ppTextures, rDescriptorInfo.iCount);
								mTextureCrcs.push_back(arrayCrc);
							}
						}
						// Register under CRC 0 for sampler recreation coverage (texture array is copied into TextureBinding)
						gpTextureManager->RegisterTextureBinding(0, this, iDescriptorCount, rDescriptorInfo.flags, nullptr, rDescriptorInfo.ppTextures, rDescriptorInfo.iCount);
					}
				}

				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(rDescriptorInfo.iCount);
				vkWriteDescriptorSet.descriptorType = rDescriptorInfo.flags & kCombinedSamplers ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				vkWriteDescriptorSet.pImageInfo = pStart;
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else
			{
				ASSERT(false);
			}

			pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
			ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
		}

		// Filter writes to only include bindings that exist in the shader layout
		// This handles sparse bindings (e.g., GLTF shadow pipelines with bindings 0, 1, 2, 15)
		if (!(mInfo.flags & kCompute))
		{
			Shader* pVertexShader = rPipelineInfo.ppShaders[0];
			Shader* pFragmentShader = rPipelineInfo.ppShaders[1];

			int64_t iValidCount = 0;
			for (int64_t j = 0; j < iDescriptorCount; ++j)
			{
				uint32_t binding = pVkWriteDescriptorSets[j].dstBinding;
				if (binding < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings)
				{
					int64_t iBind = static_cast<int64_t>(binding);
					const VkDescriptorSetLayoutBinding& vertBinding = iBind < pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pVertexShader->mInfo.pDescriptorBindings[binding] : kEmptyBinding;
					const VkDescriptorSetLayoutBinding& fragBinding = iBind < pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pFragmentShader->mInfo.pDescriptorBindings[binding] : kEmptyBinding;
					if (vertBinding.descriptorCount > 0 || fragBinding.descriptorCount > 0)
					{
						pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
					}
				}
			}
			iDescriptorCount = iValidCount;
		}

		// Route writes by set index: drop Set 0 (global), keep Set 1 and Set 2
		if (bHasExternalSet0)
		{
			Shader* pVert = rPipelineInfo.ppShaders[0];
			Shader* pFrag = rPipelineInfo.ppShaders[1];

			int64_t iValidCount = 0;
			for (int64_t j = 0; j < iDescriptorCount; ++j)
			{
				uint32_t uiBinding = pVkWriteDescriptorSets[j].dstBinding;
				uint32_t uiSet = 0;
				int64_t iBind = static_cast<int64_t>(uiBinding);
				if (iBind < pVert->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pVert->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
				{
					uiSet = pVert->mInfo.pDescriptorSetIndices[uiBinding];
				}
				else if (iBind < pFrag->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings && pFrag->mInfo.pDescriptorBindings[uiBinding].descriptorCount > 0)
				{
					uiSet = pFrag->mInfo.pDescriptorSetIndices[uiBinding];
				}

				if (uiSet == 2)
				{
					pVkWriteDescriptorSets[j].dstSet = vkDstSetSet2;
					pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
				}
				else if (uiSet == 1 && !bHasExternalSet1)
				{
					pVkWriteDescriptorSets[iValidCount++] = pVkWriteDescriptorSets[j];
				}
				// Set 0 writes are dropped (handled by global descriptor set)
			}
			iDescriptorCount = iValidCount;
		}

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(iDescriptorCount), pVkWriteDescriptorSets, 0, nullptr);
	}
}

} // namespace engine
