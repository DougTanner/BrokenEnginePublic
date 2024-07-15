#include "Pipeline.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Ui/Wrapper.h"

using namespace DirectX;

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

static VkDescriptorSetLayoutCreateInfo sUniformTextureVkDescriptorSetLayoutCreateInfo
{
	.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
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

	mInfo = rInfo;

	// Add Gltf additional automatically
	for (int64_t i = 0; i < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings; ++i)
	{
		const DescriptorInfo& rDescriptorInfo = mInfo.pDescriptorInfos[i];
		if (!(rDescriptorInfo.flags & DescriptorFlags::kGltf))
		{
			continue;
		}

		ASSERT(i + 4 < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
		ASSERT(mInfo.pDescriptorInfos[i + 1].flags == DescriptorFlags::kEmpty);

		mInfo.pDescriptorInfos[i + 1].flags = kCombinedSamplers;
		mInfo.pDescriptorInfos[i + 1].iCount = static_cast<int64_t>(std::size(gpTextureManager->mppLightingFinalTextures));
		mInfo.pDescriptorInfos[i + 1].ppTextures = gpTextureManager->mppLightingFinalTextures;

		mInfo.pDescriptorInfos[i + 2].flags = kCombinedSamplers;
		mInfo.pDescriptorInfos[i + 2].iCount = 1;
		mInfo.pDescriptorInfos[i + 2].pTexture = &gpTextureManager->mShadowBlurTexture;

		mInfo.pDescriptorInfos[i + 3].flags =  {kCombinedSamplers, kSamplerBorder};
		mInfo.pDescriptorInfos[i + 3].iCount = 1;
		mInfo.pDescriptorInfos[i + 3].pTexture = &gpTextureManager->mSmokeTextureOne;
	}

	if (!bFromMultimaterial && mInfo.pDescriptorInfos[3].flags & kGltf)
	{
		Chunk& chunk = gpFileManager->GetDataChunkMap().at(mInfo.pDescriptorInfos[3].crc);
		ASSERT(chunk.pHeader->gltfHeader.uiMaterialCount == 1);
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
}

void Pipeline::Destroy() noexcept
{
	if (mVkPipelineLayout == VK_NULL_HANDLE)
	{
		return;
	}

	vkDeviceWaitIdle(gpDeviceManager->mVkDevice);

	vkDestroyPipelineLayout(gpDeviceManager->mVkDevice, mVkPipelineLayout, nullptr);
	mVkPipelineLayout = VK_NULL_HANDLE;

	vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mVkDescriptorSetLayout, nullptr);
	mVkDescriptorSetLayout = VK_NULL_HANDLE;

	vkDestroyPipeline(gpDeviceManager->mVkDevice, mVkPipeline, nullptr);
	mVkPipeline = VK_NULL_HANDLE;

	for (VkDescriptorSet& vkDescriptorSet : mVkDescriptorSets)
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, gpDeviceManager->mVkDescriptorPool, 1, &vkDescriptorSet);
	}
	mVkDescriptorSets.clear();

	if (mIndirectVkBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(gpDeviceManager->mVkDevice, mIndirectVkBuffer, nullptr);
		mIndirectVkBuffer = VK_NULL_HANDLE;

		if (mInfo.flags & kIndirectHostVisible)
		{
			mpIndirectMappedMemory = nullptr;
			vkUnmapMemory(gpDeviceManager->mVkDevice, mIndirectVkDeviceMemory);
		}

		vkFreeMemory(gpDeviceManager->mVkDevice, mIndirectVkDeviceMemory, nullptr);
		mIndirectVkDeviceMemory = VK_NULL_HANDLE;
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
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, nullptr);
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);

	vkCmdDrawIndexed(vkCommandBuffer, static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount), static_cast<uint32_t>(iInstanceCount), 0, 0, static_cast<uint32_t>(iFirstInstance));
}

void Pipeline::RecordDrawIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const DirectX::XMFLOAT4& f4PushConstants)
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
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iCommandBuffer], 0, nullptr);
	mInfo.pVertexBuffer->RecordBindVertexBuffer(vkCommandBuffer);
	vkCmdDrawIndexedIndirect(vkCommandBuffer, mIndirectVkBuffer, mInfo.flags & kIndirectHostVisible ? iCommandBuffer * sizeof(VkDrawIndexedIndirectCommand) : 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Pipeline::RecordCompute(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ, const DirectX::XMFLOAT4& f4PushConstants)
{
	ASSERT(!(mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, 0);
	vkCmdDispatch(vkCommandBuffer, static_cast<uint32_t>(iGroupCountX), static_cast<uint32_t>(iGroupCountY), static_cast<uint32_t>(iGroupCountZ));
}

void Pipeline::RecordComputeIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const DirectX::XMFLOAT4& f4PushConstants)
{
	ASSERT((mInfo.flags & kIndirectHostVisible || mInfo.flags & kIndirectDeviceLocal) && mInfo.flags & kCompute);

	if (mInfo.flags & kPushConstants)
	{
		shaders::PushConstantsLayout pushConstantsLayout {};
		pushConstantsLayout.f4Pipeline = f4PushConstants;
		pushConstantsLayout.f4Material = {static_cast<float>(mInfo.uiMaterialIndex), 0.0f, 0.0f, 0.0f};
		vkCmdPushConstants(vkCommandBuffer, mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantsLayout), &pushConstantsLayout);
	}

	int64_t iDescriptorSetIndex = mbPerCommandBuffer ? iCommandBuffer : 0;
	vkCmdBindPipeline(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipeline);
	vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, mVkPipelineLayout, 0, 1, &mVkDescriptorSets[iDescriptorSetIndex], 0, 0);
	vkCmdDispatchIndirect(vkCommandBuffer, mIndirectVkBuffer, mInfo.flags & kIndirectHostVisible ? iCommandBuffer * sizeof(VkDispatchIndirectCommand) : 0);
}

void Pipeline::WriteIndirectBuffer(int64_t iCommandBuffer, int64_t iInstanceCount, int64_t iIndexCount, int64_t iFirstIndex)
{
	ASSERT(!(mInfo.flags & kCompute));

	VkDrawIndexedIndirectCommand& rVkDrawIndexedIndirectCommand = mpIndirectMappedMemory[iCommandBuffer];
	rVkDrawIndexedIndirectCommand.indexCount = static_cast<uint32_t>(iIndexCount == 0 ? static_cast<uint32_t>(mInfo.pVertexBuffer->mInfo.iCount) : iIndexCount);
	rVkDrawIndexedIndirectCommand.instanceCount = static_cast<uint32_t>(iInstanceCount);
	rVkDrawIndexedIndirectCommand.firstIndex = static_cast<uint32_t>(iFirstIndex);
	rVkDrawIndexedIndirectCommand.vertexOffset = 0;
	rVkDrawIndexedIndirectCommand.firstInstance = 0;
}

void Pipeline::CreatePipeline(const PipelineInfo& rPipelineInfo)
{
	ASSERT(mInfo.pcName.size() > 0);

	if (mInfo.flags & kIndirectHostVisible)
	{
		int64_t iCommandBufferCount = gpCommandBufferManager->CommandBufferCount();
		VkDeviceSize vkDeviceSize = iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand);
		Buffer::CreateBuffer(rPipelineInfo.pcName, vkDeviceSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory);
		char* pMappedMemory = nullptr;
		CHECK_VK(vkMapMemory(gpDeviceManager->mVkDevice, mIndirectVkDeviceMemory, 0, vkDeviceSize, 0, reinterpret_cast<void**>(&pMappedMemory)));
		mpIndirectMappedMemory = reinterpret_cast<VkDrawIndexedIndirectCommand*>(pMappedMemory);
	}
	else if (mInfo.flags & kIndirectDeviceLocal)
	{
		Buffer::CreateBuffer(rPipelineInfo.pcName, sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory);
	}

	Shader* pVertexShader = rPipelineInfo.ppShaders[0];
	Shader* pFragmentShader = rPipelineInfo.ppShaders[1];

	// Combine the descriptor set layouts from the vertex and fragment shaders
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iDescriptorCount = std::max(pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings, pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings);
	for (int64_t i = 0; i < iDescriptorCount; ++i)
	{
		common::MemOr(pVkDescriptorSetLayoutBindings[i], pVertexShader->mInfo.pChunkHeader->shaderHeader.pVkDescriptorSetLayoutBindings[i], pFragmentShader->mInfo.pChunkHeader->shaderHeader.pVkDescriptorSetLayoutBindings[i]);
	}
	sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
	sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkDescriptorSetLayoutBindings;
	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayout));

	// Setup pipeline layout
	sVkPipelineLayoutCreateInfo.pSetLayouts = &mVkDescriptorSetLayout;
	sVkPipelineLayoutCreateInfo.pushConstantRangeCount = mInfo.flags & kPushConstants ? 1 : 0;
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = mInfo.uiPushConstantSize;
	sVkPipelineLayoutCreateInfo.pPushConstantRanges = mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &sVkPipelineLayoutCreateInfo, nullptr, &mVkPipelineLayout));

	// Setup pipeline
	spVkPipelineShaderStageCreateInfos[0].module = pVertexShader->mVkShaderModule;
	spVkPipelineShaderStageCreateInfos[1].module = pFragmentShader->mVkShaderModule;

	ASSERT(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride == rPipelineInfo.pVertexBuffer->mInfo.iVertexStride);
	sVkVertexInputBindingDescription.stride = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride);

	sVkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputAttributeDescriptions);
	sVkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = pVertexShader->mInfo.pChunkHeader->shaderHeader.pVkVertexInputAttributeDescriptions;

	VkExtent2D vkExtent2D
	{
		.width = mInfo.flags & kRenderTarget ? mInfo.vkExtent3D.width : gpGraphics->mFramebufferExtent2D.width,
		.height = mInfo.flags & kRenderTarget ? mInfo.vkExtent3D.height : gpGraphics->mFramebufferExtent2D.height,
	};

	// NOTE: We're using VK_KHR_MAINTENANCE1_EXTENSION_NAME to support negative viewports
	//       See https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
	sVkViewport.x = 0.0f;
	sVkViewport.y = static_cast<float>(vkExtent2D.height);
	sVkViewport.width = static_cast<float>(vkExtent2D.width);
	sVkViewport.height = -static_cast<float>(vkExtent2D.height);

	sScissorVkRect2D.extent = vkExtent2D;

	sVkPipelineColorBlendAttachmentState.blendEnable = (rPipelineInfo.flags & kAlphaBlend || rPipelineInfo.flags & kAdd || rPipelineInfo.flags & kMax) ? VK_TRUE : VK_FALSE;
	sVkPipelineColorBlendAttachmentState.colorBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	sVkPipelineColorBlendAttachmentState.alphaBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	if (rPipelineInfo.flags & kAlphaBlend)
	{
		sVkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		sVkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		sVkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	}
	else
	{
		sVkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		sVkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}

#if defined(ENABLE_WIREFRAME)
	bool bWireframe = gWireframe.Get<bool>();
	if (mInfo.flags & kRenderTarget || mInfo.flags & kNoWireframe)
	{
		bWireframe = false;
	}
	sVkPipelineRasterizationStateCreateInfo.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL,
#else
	sVkPipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL,
#endif
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

	CHECK_VK(vkCreateGraphicsPipelines(gpDeviceManager->mVkDevice, VK_NULL_HANDLE, 1, &sVkGraphicsPipelineCreateInfo, nullptr, &mVkPipeline));
	VK_NAME(VK_OBJECT_TYPE_PIPELINE, mVkPipeline, mInfo.pcName.data());
}

void Pipeline::CreateComputePipeline(const PipelineInfo& rPipelineInfo)
{
	if (mInfo.flags & kIndirectHostVisible)
	{
		ASSERT(false);
	}
	else if (mInfo.flags & kIndirectDeviceLocal)
	{
		Buffer::CreateBuffer(rPipelineInfo.pcName, sizeof(VkDispatchIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mIndirectVkBuffer, mIndirectVkDeviceMemory);
	}

	Shader* pComputeShader = rPipelineInfo.ppShaders[0];

	sUniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(pComputeShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings);
	sUniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pComputeShader->mInfo.pChunkHeader->shaderHeader.pVkDescriptorSetLayoutBindings;
	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &sUniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &mVkDescriptorSetLayout));

	sVkPipelineLayoutCreateInfo.pSetLayouts = &mVkDescriptorSetLayout;
	sVkPipelineLayoutCreateInfo.pushConstantRangeCount = mInfo.flags & kPushConstants ? 1 : 0;
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = 16;
	sVkPipelineLayoutCreateInfo.pPushConstantRanges = mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &sVkPipelineLayoutCreateInfo, nullptr, &mVkPipelineLayout));

	VkComputePipelineCreateInfo vkComputePipelineCreateInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	vkComputePipelineCreateInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vkComputePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	vkComputePipelineCreateInfo.stage.module = pComputeShader->mVkShaderModule;
	vkComputePipelineCreateInfo.stage.pName = "main";
	vkComputePipelineCreateInfo.layout = mVkPipelineLayout;
	CHECK_VK(vkCreateComputePipelines(gpDeviceManager->mVkDevice, VK_NULL_HANDLE, 1, &vkComputePipelineCreateInfo, nullptr, &mVkPipeline));
	VK_NAME(VK_OBJECT_TYPE_PIPELINE, mVkPipeline, mInfo.pcName.data());
}

void Pipeline::WriteDescriptorSets(const PipelineInfo& rPipelineInfo)
{
	int64_t iPerCommandBuffer = mbPerCommandBuffer ? gpCommandBufferManager->CommandBufferCount() : 1;
	mVkDescriptorSets.resize(iPerCommandBuffer);

	for (int64_t iFramebuffer = 0; iFramebuffer < iPerCommandBuffer; ++iFramebuffer)
	{
		VkDescriptorSet& rVkDescriptorSet = mVkDescriptorSets.at(iFramebuffer);

		VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = gpDeviceManager->mVkDescriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &mVkDescriptorSetLayout,
		};
		CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &vkDescriptorSetAllocateInfo, &rVkDescriptorSet));

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

			VkWriteDescriptorSet vkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.pNext = nullptr,
				.dstSet = rVkDescriptorSet,
				.dstBinding = static_cast<uint32_t>(iDescriptorCount),
				.dstArrayElement = 0,
				// .descriptorCount
				// .descriptorType
				// .pImageInfo
				// .pBufferInfo
				.pTexelBufferView = nullptr
			};

			bool bSampler = rDescriptorInfo.flags & kSamplerClamp || rDescriptorInfo.flags & kSamplerBorder || rDescriptorInfo.flags & kSamplerRepeat || rDescriptorInfo.flags & kSamplerMirroredRepeat || rDescriptorInfo.flags & kSamplerSmoke;
			if (rDescriptorInfo.flags & kGltf)
			{
				Chunk& chunk = gpFileManager->GetDataChunkMap().at(rDescriptorInfo.crc);

				ASSERT(mInfo.uiMaterialIndex < chunk.pHeader->gltfHeader.uiMaterialCount);
				common::GltfShaderData& rGltfData = reinterpret_cast<common::GltfShaderData*>(chunk.pData)[mInfo.uiMaterialIndex];
				int64_t piTextureIndices[5] = {rGltfData.uiColorTextureIndex, rGltfData.uiPhysicalDescriptorTextureIndex, rGltfData.uiNormalTextureIndex, rGltfData.uiOcclusionTextureIndex, rGltfData.uiEmissiveTextureIndex};
				for (int64_t j = 0; j < 5; ++j)
				{
					auto& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
					ASSERT(iImageInfoCount < kiMaxImageInfos);
					rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
					ASSERT(chunk.pHeader->gltfHeader.pTextureCrcs[piTextureIndices[j]] != 0);
					rVkDescriptorImageInfo.imageView = gpTextureManager->mTextureMap.at(chunk.pHeader->gltfHeader.pTextureCrcs[piTextureIndices[j]]).mVkImageView;
					rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

					vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
					vkWriteDescriptorSet.descriptorCount = 1;
					vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
					vkWriteDescriptorSet.pBufferInfo = nullptr;

					pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
					ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				}

				// Irradiance
				auto& rVkDescriptorImageInfoIrradiance = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoIrradiance.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoIrradiance.imageView = gpTextureManager->mGltfIrradianceTexture.mVkImageView;
				rVkDescriptorImageInfoIrradiance.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoIrradiance;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				// PreFiltered
				auto& rVkDescriptorImageInfoPreFiltered = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoPreFiltered.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoPreFiltered.imageView = gpTextureManager->mGltfPreFilteredTexture.mVkImageView;
				rVkDescriptorImageInfoPreFiltered.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoPreFiltered;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				// LutBrdf
				auto& rVkDescriptorImageInfoLutBrdf = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfoLutBrdf.sampler = gpTextureManager->GetSampler(kSamplerRepeat);
				rVkDescriptorImageInfoLutBrdf.imageView = gpTextureManager->mGltfLutBrdfTexture.mVkImageView;
				rVkDescriptorImageInfoLutBrdf.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.dstBinding = static_cast<uint32_t>(iDescriptorCount);
				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfoLutBrdf;
				vkWriteDescriptorSet.pBufferInfo = nullptr;

				pVkWriteDescriptorSets[iDescriptorCount++] = vkWriteDescriptorSet;
				ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);

				// Materials
				if (mGltfMaterialsStorageBuffer.mDeviceLocalVkBuffer == VK_NULL_HANDLE)
				{
					mGltfMaterialsStorageBuffer.Create(
					{
						.pcName = "Materials",
						.flags = {BufferFlags::kStorage, BufferFlags::kDeviceLocal},
						.dataVkDeviceSize = chunk.pHeader->gltfHeader.uiMaterialCount * sizeof(shaders::GltfMaterialLayout),
					},
					[&](void* pData)
					{
						shaders::GltfMaterialLayout* pCurrent = reinterpret_cast<shaders::GltfMaterialLayout*>(pData);
						for (int64_t j = 0; j < chunk.pHeader->gltfHeader.uiMaterialCount; ++j)
						{
							auto pGltfShaderData = reinterpret_cast<common::GltfShaderData*>(chunk.pData);
							memcpy(pCurrent++, &pGltfShaderData[j].f4BaseColorFactor, sizeof(shaders::GltfMaterialLayout));
						}
					});
				}

				auto& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[iBufferInfoCount++];
				ASSERT(iBufferInfoCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
				rVkDescriptorBufferInfo.buffer = mGltfMaterialsStorageBuffer.mDeviceLocalVkBuffer;
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

				auto& rVkDescriptorBufferInfo = pVkDescriptorBufferInfos[iBufferInfoCount++];
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
				auto& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
				ASSERT(iImageInfoCount < kiMaxImageInfos);
				rVkDescriptorImageInfo.sampler = gpTextureManager->GetSampler(rDescriptorInfo.flags);
				rVkDescriptorImageInfo.imageView = nullptr;
				rVkDescriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

				vkWriteDescriptorSet.descriptorCount = 1;
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				vkWriteDescriptorSet.pImageInfo = &rVkDescriptorImageInfo;
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else if (rDescriptorInfo.flags & kTextures)
			{
				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mImageInfos.size());
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				vkWriteDescriptorSet.pImageInfo = gpTextureManager->mImageInfos.data();
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else if (rDescriptorInfo.flags & kUiTextures)
			{
				vkWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(gpTextureManager->mUiImageInfos.size());
				vkWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				vkWriteDescriptorSet.pImageInfo = gpTextureManager->mUiImageInfos.data();
				vkWriteDescriptorSet.pBufferInfo = nullptr;
			}
			else if (rDescriptorInfo.flags & kCombinedSamplers || rDescriptorInfo.flags & kStorageImages)
			{
				VkDescriptorImageInfo* pStart = &pVkDescriptorImageInfos[iImageInfoCount];

				for (int64_t k = 0; k < rDescriptorInfo.iCount; ++k)
				{
					auto& rVkDescriptorImageInfo = pVkDescriptorImageInfos[iImageInfoCount++];
					ASSERT(iImageInfoCount < kiMaxImageInfos);
					rVkDescriptorImageInfo.sampler = rDescriptorInfo.flags & kCombinedSamplers ? gpTextureManager->GetSampler(rDescriptorInfo.flags) : nullptr;
					rVkDescriptorImageInfo.imageView = rDescriptorInfo.iCount == 1 && rDescriptorInfo.pTexture != nullptr ? rDescriptorInfo.pTexture->mVkImageView : rDescriptorInfo.ppTextures[k]->mVkImageView;
					rVkDescriptorImageInfo.imageLayout = rDescriptorInfo.flags & kCombinedSamplers ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
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

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(iDescriptorCount), pVkWriteDescriptorSets, 0, nullptr);

		if (!(mInfo.flags & kCompute))
		{
			Shader* pVertexShader = rPipelineInfo.ppShaders[0];
			Shader* pFragmentShader = rPipelineInfo.ppShaders[1];
			int64_t iShaderDescriptorCount = std::max(pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings, pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings);
			ASSERT(iShaderDescriptorCount == iDescriptorCount);
		}
	}
}

} // namespace engine
