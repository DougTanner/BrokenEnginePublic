#include "PipelineCreator.h"

#include "Pipeline.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

// Configures update-after-bind for storage buffer bindings in dynamic pipelines
static void ConfigureUpdateAfterBind(VkDescriptorSetLayoutCreateInfo& rLayoutCreateInfo, const VkDescriptorSetLayoutBinding* pBindings, int64_t iDescriptorCount, VkDescriptorBindingFlags* pBindingFlags, VkDescriptorSetLayoutBindingFlagsCreateInfo& rBindingFlagsCreateInfo, bool bUpdateAfterBind)
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
		rLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		rLayoutCreateInfo.pNext = &rBindingFlagsCreateInfo;
	}
	else
	{
		rLayoutCreateInfo.flags = 0;
		rLayoutCreateInfo.pNext = nullptr;
	}
}

void PipelineCreator::CreateGraphicsPipeline(Pipeline& rPipeline, const PipelineInfo& rPipelineInfo)
{
	ASSERT(rPipeline.mInfo.name.size() > 0);

	// Pipeline state structs (initialized with defaults, modified per-pipeline below)
	VkDescriptorSetLayoutCreateInfo uniformTextureVkDescriptorSetLayoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		// .bindingCount
		// .pBindings
	};

	VkPipelineLayoutCreateInfo vkPipelineLayoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		// .pSetLayouts
		// .pushConstantRangeCount
		// .pPushConstantRanges
	};

	VkPipelineShaderStageCreateInfo pVkPipelineShaderStageCreateInfos[]
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

	VkVertexInputBindingDescription vkVertexInputBindingDescription
	{
		.binding = 0,
		// .stride
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkPipelineVertexInputStateCreateInfo vkPipelineVertexInputStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vkVertexInputBindingDescription,
		// .vertexAttributeDescriptionCount
		// .pVertexAttributeDescriptions
	};

	VkPipelineInputAssemblyStateCreateInfo vkPipelineInputAssemblyStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkViewport vkViewport
	{
		.x = 0.0f,
		.y = 0.0f,
		// .width
		// .height
		.minDepth = kfMinDepth,
		.maxDepth = kfMaxDepth,
	};

	VkRect2D scissorVkRect2D
	{
		.offset = VkOffset2D {.x = 0, .y = 0},
		// .extent
	};

	VkPipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = &vkViewport,
		.scissorCount = 1,
		.pScissors = &scissorVkRect2D,
	};

	VkPipelineRasterizationStateCreateInfo vkPipelineRasterizationStateCreateInfo
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

	VkPipelineMultisampleStateCreateInfo vkPipelineMultisampleStateCreateInfo
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

	VkPipelineDepthStencilStateCreateInfo vkPipelineDepthStencilStateCreateInfo
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

	VkPipelineColorBlendAttachmentState vkPipelineColorBlendAttachmentState
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

	VkPipelineColorBlendStateCreateInfo vkPipelineColorBlendStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &vkPipelineColorBlendAttachmentState,
		.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
	};

	VkGraphicsPipelineCreateInfo vkGraphicsPipelineCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stageCount = 2,
		.pStages = pVkPipelineShaderStageCreateInfos,
		.pVertexInputState = &vkPipelineVertexInputStateCreateInfo,
		.pInputAssemblyState = &vkPipelineInputAssemblyStateCreateInfo,
		.pViewportState = &vkPipelineViewportStateCreateInfo,
		.pRasterizationState = &vkPipelineRasterizationStateCreateInfo,
		.pMultisampleState = &vkPipelineMultisampleStateCreateInfo,
		.pDepthStencilState = &vkPipelineDepthStencilStateCreateInfo,
		.pColorBlendState = &vkPipelineColorBlendStateCreateInfo,
		.pDynamicState = nullptr,
		// .layout
		// .renderPass
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};

	if (rPipeline.mInfo.flags & kIndirectHostVisible)
	{
		// Use max of actual count and 3 to handle swapchain recreation scenarios
		size_t uiFramebufferCount = gpSwapchainManager->mFramebuffers.size();
		int64_t iCommandBufferCount = std::max(uiFramebufferCount, static_cast<size_t>(3));
		VkDeviceSize vkDeviceSize = iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand);
		VmaAllocationInfo vmaAllocationInfo {};
		Buffer::CreateBuffer(rPipelineInfo.name, vkDeviceSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVkDeviceMemory, rPipeline.mIndirectVmaAllocation, &vmaAllocationInfo);

		// Verify VMA gave us the memory properties we requested
		VkMemoryPropertyFlags vkMemoryPropertyFlags = 0;
		vmaGetAllocationMemoryProperties(gpDeviceManager->mpAllocator, rPipeline.mIndirectVmaAllocation, &vkMemoryPropertyFlags);
		ASSERT((vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
		ASSERT((vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);

		rPipeline.mpIndirectMappedMemory = static_cast<VkDrawIndexedIndirectCommand*>(vmaAllocationInfo.pMappedData);

		// Initialize all indirect buffer slots to zero
		for (int64_t i = 0; i < iCommandBufferCount; ++i)
		{
			VkDrawIndexedIndirectCommand& rCommand = rPipeline.mpIndirectMappedMemory[i];
			rCommand.indexCount = 0;
			rCommand.instanceCount = 0;
			rCommand.firstIndex = 0;
			rCommand.vertexOffset = 0;
			rCommand.firstInstance = 0;
		}
	}
	else if (rPipeline.mInfo.flags & kIndirectDeviceLocal)
	{
		// Use max of actual count and 3 to handle swapchain recreation scenarios
		size_t uiFramebufferCount = gpSwapchainManager->mFramebuffers.size();
		int64_t iCommandBufferCount = std::max(uiFramebufferCount, static_cast<size_t>(3));
		VkDeviceSize vkDeviceSize = iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand);
		Buffer::CreateBuffer(rPipelineInfo.name, vkDeviceSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVkDeviceMemory, rPipeline.mIndirectVmaAllocation);
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
		const VkDescriptorSetLayoutBinding& rVertexBinding = i < pVertexShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pVertexShader->mInfo.pDescriptorBindings[i] : Pipeline::kEmptyBinding;
		const VkDescriptorSetLayoutBinding& rFragmentBinding = i < pFragmentShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? pFragmentShader->mInfo.pDescriptorBindings[i] : Pipeline::kEmptyBinding;

		// Skip empty gap entries - they would all have binding=0 causing duplicates
		if (rVertexBinding.descriptorCount == 0 && rFragmentBinding.descriptorCount == 0)
		{
			continue;
		}

		pVkDescriptorSetLayoutBindings[iDescriptorCount].binding = rVertexBinding.binding | rFragmentBinding.binding;
		if (rVertexBinding.descriptorCount > 0 && rFragmentBinding.descriptorCount > 0)
		{
			ASSERT(rVertexBinding.descriptorType == rFragmentBinding.descriptorType);
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount].descriptorType = rVertexBinding.descriptorCount > 0 ? rVertexBinding.descriptorType : rFragmentBinding.descriptorType;
		uint32_t uiDescriptorCount = std::max(rVertexBinding.descriptorCount, rFragmentBinding.descriptorCount);
		// Runtime-sized arrays exported with UINT32_MAX sentinel; replace with actual texture array size
		if (uiDescriptorCount == UINT32_MAX)
		{
			uiDescriptorCount = static_cast<uint32_t>(gpTextureManager->mTextureDescriptors.mImageInfos.size());
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount].descriptorCount = uiDescriptorCount;
		pVkDescriptorSetLayoutBindings[iDescriptorCount].stageFlags = rVertexBinding.stageFlags | rFragmentBinding.stageFlags;
		pVkDescriptorSetLayoutBindings[iDescriptorCount].pImmutableSamplers = rVertexBinding.pImmutableSamplers != nullptr ? rVertexBinding.pImmutableSamplers : rFragmentBinding.pImmutableSamplers;
		++iDescriptorCount;
	}
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = rPipeline.mInfo.uiPushConstantSize;

	if (rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
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
		if (rPipeline.mVkExternalDescriptorSetLayoutSet1 == VK_NULL_HANDLE)
		{
			uniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet1Count);
			uniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkSet1Bindings;
			VkDescriptorBindingFlags pBindingFlagsSet1[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet1 {};
			ConfigureUpdateAfterBind(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkSet1Bindings, iSet1Count, pBindingFlagsSet1, bindingFlagsCreateInfoSet1, rPipeline.mInfo.flags & kUpdateAfterBind);
			CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &uniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());
		}

		// Create Set 2 layout (kMultiSet models only)
		if (rPipeline.mInfo.flags & kMultiSet)
		{
			uniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet2Count);
			uniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkSet2Bindings;
			VkDescriptorBindingFlags pBindingFlagsSet2[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet2 {};
			ConfigureUpdateAfterBind(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkSet2Bindings, iSet2Count, pBindingFlagsSet2, bindingFlagsCreateInfoSet2, rPipeline.mInfo.flags & kUpdateAfterBind);
			CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &uniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayoutSet2));
			VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayoutSet2, rPipeline.mInfo.name.data());
		}

		// Pipeline layout: [global Set 0, Set 1, optional Set 2]
		VkDescriptorSetLayout pSetLayouts[3] =
		{
			rPipeline.mVkExternalDescriptorSetLayout,
			rPipeline.mVkExternalDescriptorSetLayoutSet1 != VK_NULL_HANDLE ? rPipeline.mVkExternalDescriptorSetLayoutSet1 : rPipeline.mVkDescriptorSetLayout,
			rPipeline.mVkDescriptorSetLayoutSet2,
		};
		uint32_t uiSetCount = (rPipeline.mInfo.flags & kMultiSet) ? 3 : 2;
		vkPipelineLayoutCreateInfo.setLayoutCount = uiSetCount;
		vkPipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
		vkPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
		vkPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
		CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &vkPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
		VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());
	}
	else
	{
		// Compute pipelines or pipelines without global Set 0: single descriptor set
		uniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
		uniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkDescriptorSetLayoutBindings;

		VkDescriptorBindingFlags pBindingFlags[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo {};
		ConfigureUpdateAfterBind(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkDescriptorSetLayoutBindings, iDescriptorCount, pBindingFlags, bindingFlagsCreateInfo, rPipeline.mInfo.flags & kUpdateAfterBind);

		CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &uniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());

		vkPipelineLayoutCreateInfo.pSetLayouts = &rPipeline.mVkDescriptorSetLayout;
		vkPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
		vkPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
		CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &vkPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
		VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());
	}

	// Setup pipeline
	pVkPipelineShaderStageCreateInfos[0].module = pVertexShader->mVkShaderModule;
	pVkPipelineShaderStageCreateInfos[1].module = pFragmentShader->mVkShaderModule;

	ASSERT(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride == rPipelineInfo.pVertexBuffer->mInfo.iVertexStride);
	vkVertexInputBindingDescription.stride = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride);

	vkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputAttributeDescriptions);
	vkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = pVertexShader->mInfo.pVertexAttributes;

	VkExtent2D vkExtent2D
	{
		.width = rPipeline.mInfo.flags & kRenderTarget ? rPipeline.mInfo.vkExtent3D.width : gpGraphics->mFramebufferExtent2D.width,
		.height = rPipeline.mInfo.flags & kRenderTarget ? rPipeline.mInfo.vkExtent3D.height : gpGraphics->mFramebufferExtent2D.height,
	};

	// Negative viewport height (VK_KHR_maintenance1) flips the Vulkan Y axis to match DirectX convention
	vkViewport.x = 0.0f;
	vkViewport.y = static_cast<float>(vkExtent2D.height);
	vkViewport.width = static_cast<float>(vkExtent2D.width);
	vkViewport.height = -static_cast<float>(vkExtent2D.height);

	scissorVkRect2D.extent = vkExtent2D;

	vkPipelineColorBlendAttachmentState.blendEnable = (rPipelineInfo.flags & kAlphaBlend || rPipelineInfo.flags & kAdd || rPipelineInfo.flags & kAddAlpha || rPipelineInfo.flags & kMax) ? VK_TRUE : VK_FALSE;
	vkPipelineColorBlendAttachmentState.colorBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	vkPipelineColorBlendAttachmentState.alphaBlendOp = rPipelineInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	if (rPipelineInfo.flags & kAlphaBlend)
	{
		vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	}
	else if (rPipelineInfo.flags & kAddAlpha)
	{
		vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else
	{
		vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}

	if constexpr (kbEnableWireframe)
	{
		bool bWireframe = gWireframe.Get<bool>();
		if (rPipeline.mInfo.flags & kRenderTarget || rPipeline.mInfo.flags & kNoWireframe)
		{
			bWireframe = false;
		}
		vkPipelineRasterizationStateCreateInfo.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	}
	else
	{
		vkPipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	}
	vkPipelineRasterizationStateCreateInfo.cullMode = rPipelineInfo.flags & kCullBack ? VK_CULL_MODE_BACK_BIT : (rPipelineInfo.flags & kCullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE);
	if (rPipelineInfo.flags & kDepthBias)
	{
		vkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_TRUE;
		vkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = -3.0f;
		vkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		vkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = -3.0f;
	}
	else
	{
		vkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		vkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
		vkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		vkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
	}

	vkPipelineDepthStencilStateCreateInfo.depthTestEnable = rPipelineInfo.flags & kDepthTest ? VK_TRUE : VK_FALSE;
	vkPipelineDepthStencilStateCreateInfo.depthWriteEnable = rPipelineInfo.flags & kDepthWrite ? VK_TRUE : VK_FALSE;

	vkPipelineMultisampleStateCreateInfo.rasterizationSamples = rPipeline.mInfo.flags & kRenderTarget ? VK_SAMPLE_COUNT_1_BIT : (gMultisampling.Get<bool>() ? gSampleCount.Get<VkSampleCountFlagBits>() : VK_SAMPLE_COUNT_1_BIT);
	vkPipelineMultisampleStateCreateInfo.sampleShadingEnable = (rPipelineInfo.flags & kSampleShading && gSampleShading.Get<bool>()) ? VK_TRUE : VK_FALSE;
	vkPipelineMultisampleStateCreateInfo.minSampleShading = gMinSampleShading.Get();

	vkGraphicsPipelineCreateInfo.layout = rPipeline.mVkPipelineLayout;
	vkGraphicsPipelineCreateInfo.renderPass = rPipeline.mInfo.flags & kRenderTarget ? rPipelineInfo.vkRenderPass : gpSwapchainManager->mVkRenderPass;

	// Configure MRT blend states
	int32_t iColorAttachmentCount = rPipelineInfo.iColorAttachmentCount;
	if (iColorAttachmentCount == 1 && rPipelineInfo.vkRenderPass == gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass)
	{
		iColorAttachmentCount = 3;
	}
	VkPipelineColorBlendAttachmentState pMrtBlendStates[3] = {};
	if (iColorAttachmentCount > 1)
	{
		for (int64_t i = 0; i < iColorAttachmentCount; ++i)
		{
			pMrtBlendStates[i] = vkPipelineColorBlendAttachmentState;
		}
		vkPipelineColorBlendStateCreateInfo.attachmentCount = iColorAttachmentCount;
		vkPipelineColorBlendStateCreateInfo.pAttachments = pMrtBlendStates;
	}
	else
	{
		vkPipelineColorBlendStateCreateInfo.attachmentCount = 1;
		vkPipelineColorBlendStateCreateInfo.pAttachments = &vkPipelineColorBlendAttachmentState;
	}

	CHECK_VK(vkCreateGraphicsPipelines(gpDeviceManager->mVkDevice, gpDeviceManager->mVkPipelineCache, 1, &vkGraphicsPipelineCreateInfo, nullptr, &rPipeline.mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, rPipeline.mVkPipeline, rPipeline.mInfo.name.data());
}

void PipelineCreator::CreateComputePipeline(Pipeline& rPipeline, const PipelineInfo& rPipelineInfo)
{
	VkDescriptorSetLayoutCreateInfo uniformTextureVkDescriptorSetLayoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		// .bindingCount
		// .pBindings
	};

	VkPipelineLayoutCreateInfo vkPipelineLayoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		// .pSetLayouts
		// .pushConstantRangeCount
		// .pPushConstantRanges
	};

	if (rPipeline.mInfo.flags & kIndirectHostVisible)
	{
		ASSERT(false);
	}
	else if (rPipeline.mInfo.flags & kIndirectDeviceLocal)
	{
		Buffer::CreateBuffer(rPipelineInfo.name, sizeof(VkDispatchIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVkDeviceMemory, rPipeline.mIndirectVmaAllocation);
	}

	Shader* pComputeShader = rPipelineInfo.ppShaders[0];

	// Filter out empty entries to avoid duplicate binding 0 errors from zero-initialized gaps
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iSourceCount = pComputeShader->mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings;
	int64_t iDescriptorCount = 0;
	for (int64_t i = 0; i < iSourceCount; ++i)
	{
		const VkDescriptorSetLayoutBinding& rBinding = pComputeShader->mInfo.pDescriptorBindings[i];
		if (rBinding.descriptorCount == 0)
		{
			continue;
		}
		pVkDescriptorSetLayoutBindings[iDescriptorCount++] = rBinding;
	}
	uniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
	uniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkDescriptorSetLayoutBindings;

	VkDescriptorBindingFlags pBindingFlags[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo {};
	ConfigureUpdateAfterBind(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkDescriptorSetLayoutBindings, iDescriptorCount, pBindingFlags, bindingFlagsCreateInfo, rPipeline.mInfo.flags & kUpdateAfterBind);

	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &uniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());

	vkPipelineLayoutCreateInfo.pSetLayouts = &rPipeline.mVkDescriptorSetLayout;
	vkPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = rPipeline.mInfo.uiPushConstantSize;
	vkPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &vkPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
	VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());

	VkComputePipelineCreateInfo vkComputePipelineCreateInfo = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	vkComputePipelineCreateInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vkComputePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	vkComputePipelineCreateInfo.stage.module = pComputeShader->mVkShaderModule;
	vkComputePipelineCreateInfo.stage.pName = "main";
	vkComputePipelineCreateInfo.layout = rPipeline.mVkPipelineLayout;
	CHECK_VK(vkCreateComputePipelines(gpDeviceManager->mVkDevice, gpDeviceManager->mVkPipelineCache, 1, &vkComputePipelineCreateInfo, nullptr, &rPipeline.mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, rPipeline.mVkPipeline, rPipeline.mInfo.name.data());
}

} // namespace engine
