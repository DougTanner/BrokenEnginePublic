#if defined(BT_CLIENT)

#include "PipelineCreator.h"

#include "Pipeline.h"

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

using enum DescriptorFlags;
using enum PipelineFlags;

static constexpr float kfDepthBiasConstantFactor = -3.0f;
static constexpr float kfDepthBiasSlopeFactor = -3.0f;
static constexpr int64_t kiMaxColorAttachments = 6;

// Push-constant range size for a pipeline's layout: the per-pipeline override when set, else the default
// 16-byte PushConstantsLayout. It must match the shader's declared push-constant block.
static uint32_t ResolvePushConstantBytes(const Pipeline& rPipeline)
{
	return rPipeline.mInfo.iPushConstantBytes > 0 ? static_cast<uint32_t>(rPipeline.mInfo.iPushConstantBytes) : static_cast<uint32_t>(sizeof(shaders::PushConstantsLayout));
}

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

				// Array bindings (e.g. the island bindless terrain color/normals/AO/masks/elevation
				// arrays) have lazily-minted and LRU-evicted slots that legitimately point at a freed
				// image view until re-mint. Mark such arrays partially bound so a stale, not-
				// dynamically-accessed slot (the evicted island's quad is zero-width-culled) is
				// spec-legal. Harmless on fully-populated arrays — PARTIALLY_BOUND only relaxes the
				// "every statically-used element must be valid" rule and never alters rendered output.
				if (pBindings[i].descriptorCount > 1)
				{
					pBindingFlags[i] |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
				}
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

static void CreateSingleSetPipelineLayout(VkDescriptorSetLayoutCreateInfo& rLayoutCreateInfo, const VkDescriptorSetLayoutBinding* pBindings, int64_t iDescriptorCount, Pipeline& rPipeline, VkPipelineLayoutCreateInfo& rPipelineLayoutCreateInfo, VkShaderStageFlags vkPushConstantStageFlags)
{
	rLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iDescriptorCount);
	rLayoutCreateInfo.pBindings = pBindings;

	VkDescriptorBindingFlags pBindingFlags[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo {};
	ConfigureUpdateAfterBind(rLayoutCreateInfo, pBindings, iDescriptorCount, pBindingFlags, bindingFlagsCreateInfo, rPipeline.mInfo.flags & kUpdateAfterBind);

	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &rLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());

	rPipelineLayoutCreateInfo.pSetLayouts = &rPipeline.mVkDescriptorSetLayout;
	rPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = vkPushConstantStageFlags;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = ResolvePushConstantBytes(rPipeline);
	rPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &rPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
	VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());
}

// Creates a host-visible/coherent indirect buffer of iSlotCount * iElementSize bytes, asserts VMA honored the
// host-visible + coherent request, and zeroes the persistent mapping so no slot runs until the CPU writes real
// data. Shared by the graphics draw-indirect and compute dispatch-indirect host-visible paths — they differ
// only in element/command type (VkDrawIndexedIndirectCommand vs VkDispatchIndirectCommand) and which typed
// mapped-pointer member the caller stores the result in; every command struct is fully zero-cleared, so a byte
// memset over the whole range is equivalent to the old per-field init. Returns the mapped pointer. NOTE: never
// route the device-local paths through here (graphics N-slot / compute single-slot device-local are GPU-written,
// not host-mapped).
static void* CreateHostVisibleIndirectBuffer(Pipeline& rPipeline, int64_t iSlotCount, int64_t iElementSize)
{
	VmaAllocationInfo vmaAllocationInfo {};
	Buffer::CreateBuffer(rPipeline.mInfo.name, iSlotCount * iElementSize, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVmaAllocation, &vmaAllocationInfo);

	// Verify VMA gave us the memory properties we requested
	VkMemoryPropertyFlags vkMemoryPropertyFlags = 0;
	vmaGetAllocationMemoryProperties(gpDeviceManager->mpAllocator, rPipeline.mIndirectVmaAllocation, &vkMemoryPropertyFlags);
	ASSERT((vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
	ASSERT((vkMemoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0);

	ASSERT(vmaAllocationInfo.pMappedData != nullptr);

	// Zero all slots — no work runs until the CPU writes real data
	std::memset(vmaAllocationInfo.pMappedData, 0, static_cast<size_t>(iSlotCount * iElementSize));
	return vmaAllocationInfo.pMappedData;
}

static void SetupIndirectBuffer(Pipeline& rPipeline, int64_t iCommandBufferCount)
{
	if (rPipeline.mInfo.flags & kIndirectHostVisible)
	{
		rPipeline.miIndirectSlotCount = iCommandBufferCount;
		rPipeline.mpIndirectMappedMemory = static_cast<VkDrawIndexedIndirectCommand*>(CreateHostVisibleIndirectBuffer(rPipeline, iCommandBufferCount, sizeof(VkDrawIndexedIndirectCommand)));
	}
	else if (rPipeline.mInfo.flags & kIndirectDeviceLocal)
	{
		rPipeline.miIndirectSlotCount = iCommandBufferCount;
		Buffer::CreateBuffer(rPipeline.mInfo.name, iCommandBufferCount * sizeof(VkDrawIndexedIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVmaAllocation);
	}
}

// CreateDescriptorSetLayouts stage 1: merge the vertex + fragment shaders' reflected bindings into one
// array, filtering the zero-initialized gap entries (which would all collide at binding 0). Returns count.
static int64_t MergeReflectedBindings(const Shader& rVertexShader, const Shader& rFragmentShader, VkDescriptorSetLayoutBinding* pVkDescriptorSetLayoutBindings)
{
	int64_t iSourceCount = std::max(rVertexShader.mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings, rFragmentShader.mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings);
	int64_t iDescriptorCount = 0;
	for (int64_t i = 0; i < iSourceCount; ++i)
	{
		const VkDescriptorSetLayoutBinding& rVertexBinding = i < rVertexShader.mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rVertexShader.mInfo.pDescriptorBindings[i] : Pipeline::kEmptyBinding;
		const VkDescriptorSetLayoutBinding& rFragmentBinding = i < rFragmentShader.mInfo.pChunkHeader->shaderHeader.iDescriptorSetLayoutBindings ? rFragmentShader.mInfo.pDescriptorBindings[i] : Pipeline::kEmptyBinding;

		// Skip empty gap entries - they would all have binding=0 causing duplicates
		if (rVertexBinding.descriptorCount == 0 && rFragmentBinding.descriptorCount == 0)
		{
			continue;
		}

		ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
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
	return iDescriptorCount;
}

// CreateDescriptorSetLayouts stage 2: partition the merged bindings into the per-pipeline Set 1 / Set 2
// arrays by shader-reflected set index. Set 0 bindings are handled by the global descriptor set and dropped.
static void SplitBindingsBySet(const Pipeline& rPipeline, const VkDescriptorSetLayoutBinding* pVkDescriptorSetLayoutBindings, int64_t iDescriptorCount, VkDescriptorSetLayoutBinding* pVkSet1Bindings, int64_t& riSet1Count, VkDescriptorSetLayoutBinding* pVkSet2Bindings, int64_t& riSet2Count)
{
	riSet1Count = 0;
	riSet2Count = 0;
	for (int64_t i = 0; i < iDescriptorCount; ++i)
	{
		uint32_t uiBinding = pVkDescriptorSetLayoutBindings[i].binding;
		uint32_t uiSet = Pipeline::ResolveBindingSetIndex(rPipeline.mInfo, uiBinding);

		if (uiSet == 1)
		{
			pVkSet1Bindings[riSet1Count++] = pVkDescriptorSetLayoutBindings[i];
		}
		else if (uiSet == 2)
		{
			pVkSet2Bindings[riSet2Count++] = pVkDescriptorSetLayoutBindings[i];
		}
		// Set 0 bindings are handled by global descriptor set
	}
}

// CreateDescriptorSetLayouts stage 3: create the per-pipeline Set 1 (+ optional Set 2) descriptor set
// layouts and assemble the [global Set 0, Set 1, optional Set 2] pipeline layout.
static void CreateMultiSetPipelineLayout(Pipeline& rPipeline, VkDescriptorSetLayoutCreateInfo& rLayoutCreateInfo, VkPipelineLayoutCreateInfo& rPipelineLayoutCreateInfo, const VkPushConstantRange& rVkPushConstantRange, const VkDescriptorSetLayoutBinding* pVkSet1Bindings, int64_t iSet1Count, const VkDescriptorSetLayoutBinding* pVkSet2Bindings, int64_t iSet2Count)
{
	// Create Set 1 layout (unless using external layout from first ModelPipeline material)
	if (rPipeline.mVkExternalDescriptorSetLayoutSet1 == VK_NULL_HANDLE)
	{
		rLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet1Count);
		rLayoutCreateInfo.pBindings = pVkSet1Bindings;
		VkDescriptorBindingFlags pBindingFlagsSet1[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet1 {};
		ConfigureUpdateAfterBind(rLayoutCreateInfo, pVkSet1Bindings, iSet1Count, pBindingFlagsSet1, bindingFlagsCreateInfoSet1, rPipeline.mInfo.flags & kUpdateAfterBind);
		CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &rLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());
	}

	// Create Set 2 layout (kMultiSet models only)
	if (rPipeline.mInfo.flags & kMultiSet)
	{
		rLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet2Count);
		rLayoutCreateInfo.pBindings = pVkSet2Bindings;
		VkDescriptorBindingFlags pBindingFlagsSet2[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet2 {};
		ConfigureUpdateAfterBind(rLayoutCreateInfo, pVkSet2Bindings, iSet2Count, pBindingFlagsSet2, bindingFlagsCreateInfoSet2, rPipeline.mInfo.flags & kUpdateAfterBind);
		CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &rLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayoutSet2));
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
	rPipelineLayoutCreateInfo.setLayoutCount = uiSetCount;
	rPipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
	rPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
	rPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &rVkPushConstantRange : nullptr;
	CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &rPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
	VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());
}

static void CreateDescriptorSetLayouts(Pipeline& rPipeline, VkDescriptorSetLayoutCreateInfo& rLayoutCreateInfo, VkPipelineLayoutCreateInfo& rPipelineLayoutCreateInfo)
{
	VkDescriptorSetLayoutBinding pVkDescriptorSetLayoutBindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iDescriptorCount = MergeReflectedBindings(*rPipeline.mInfo.ppShaders[0], *rPipeline.mInfo.ppShaders[1], pVkDescriptorSetLayoutBindings);

	VkPushConstantRange vkPushConstantRange {};
	vkPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	vkPushConstantRange.offset = 0;
	vkPushConstantRange.size = ResolvePushConstantBytes(rPipeline);

	if (rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		// Graphics pipeline with global Set 0: split bindings by shader reflection set index
		VkDescriptorSetLayoutBinding pVkSet1Bindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBinding pVkSet2Bindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		int64_t iSet1Count = 0;
		int64_t iSet2Count = 0;
		SplitBindingsBySet(rPipeline, pVkDescriptorSetLayoutBindings, iDescriptorCount, pVkSet1Bindings, iSet1Count, pVkSet2Bindings, iSet2Count);
		CreateMultiSetPipelineLayout(rPipeline, rLayoutCreateInfo, rPipelineLayoutCreateInfo, vkPushConstantRange, pVkSet1Bindings, iSet1Count, pVkSet2Bindings, iSet2Count);
	}
	else
	{
		CreateSingleSetPipelineLayout(rLayoutCreateInfo, pVkDescriptorSetLayoutBindings, iDescriptorCount, rPipeline, rPipelineLayoutCreateInfo, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
}

// Owns the Vk*StateCreateInfo structs (and the sub-structs / arrays they point at) for one graphics
// pipeline so they outlive the vkCreateGraphicsPipelines call; the Configure* stages populate it in place.
struct GraphicsPipelineState
{
	VkVertexInputBindingDescription vkVertexInputBindingDescription;
	VkPipelineVertexInputStateCreateInfo vkPipelineVertexInputStateCreateInfo;
	VkPipelineInputAssemblyStateCreateInfo vkPipelineInputAssemblyStateCreateInfo;
	VkViewport vkViewport;
	VkRect2D scissorVkRect2D;
	VkPipelineViewportStateCreateInfo vkPipelineViewportStateCreateInfo;
	VkPipelineRasterizationLineStateCreateInfoEXT vkPipelineRasterizationLineStateCreateInfoEXT;
	VkPipelineRasterizationStateCreateInfo vkPipelineRasterizationStateCreateInfo;
	VkPipelineMultisampleStateCreateInfo vkPipelineMultisampleStateCreateInfo;
	VkPipelineDepthStencilStateCreateInfo vkPipelineDepthStencilStateCreateInfo;
	VkPipelineColorBlendAttachmentState vkPipelineColorBlendAttachmentState;
	VkPipelineColorBlendStateCreateInfo vkPipelineColorBlendStateCreateInfo;
	VkPipelineColorBlendAttachmentState pMrtBlendStates[kiMaxColorAttachments];
};

// Builds the vertex-input state: stride + attribute descriptions come from shader reflection (or are
// zeroed for stride-0 fullscreen passes); a bound vertex buffer asserts a matching reflected stride.
static void ConfigureVertexInput(GraphicsPipelineState& rState, const Pipeline& rPipeline)
{
	rState.vkVertexInputBindingDescription = VkVertexInputBindingDescription
	{
		.binding = 0,
		// .stride
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	rState.vkPipelineVertexInputStateCreateInfo = VkPipelineVertexInputStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &rState.vkVertexInputBindingDescription,
		// .vertexAttributeDescriptionCount
		// .pVertexAttributeDescriptions
	};

	Shader* pVertexShader = rPipeline.mInfo.ppShaders[0];
	if (rPipeline.mInfo.pVertexBuffer != nullptr)
	{
		ASSERT(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride == rPipeline.mInfo.pVertexBuffer->mInfo.iVertexStride);
		rState.vkVertexInputBindingDescription.stride = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride);
		rState.vkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputAttributeDescriptions);
		rState.vkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = pVertexShader->mInfo.pVertexAttributes;
	}
	else if (pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride > 0)
	{
		// Per-draw vertex buffer binding (e.g., per-island terrain meshes): no single canonical
		// vertex buffer is owned by the pipeline. Stride and attributes come from shader reflection;
		// the caller binds the actual buffer via vkCmdBindVertexBuffers at draw time.
		rState.vkVertexInputBindingDescription.stride = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputStride);
		rState.vkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(pVertexShader->mInfo.pChunkHeader->shaderHeader.iVertexInputAttributeDescriptions);
		rState.vkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = pVertexShader->mInfo.pVertexAttributes;
	}
	else
	{
		rState.vkPipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 0;
		rState.vkPipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = nullptr;
		rState.vkPipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = 0;
		rState.vkPipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = nullptr;
	}
}

// Builds the viewport/scissor state sized to the render-target extent (or the framebuffer extent for
// swapchain passes); the viewport uses negative height to flip Vulkan's Y axis to the DirectX convention.
static void ConfigureViewportScissor(GraphicsPipelineState& rState, const Pipeline& rPipeline)
{
	rState.vkViewport = VkViewport
	{
		.x = 0.0f,
		.y = 0.0f,
		// .width
		// .height
		.minDepth = kfMinDepth,
		.maxDepth = kfMaxDepth,
	};

	rState.scissorVkRect2D = VkRect2D
	{
		.offset = VkOffset2D {.x = 0, .y = 0},
		// .extent
	};

	rState.vkPipelineViewportStateCreateInfo = VkPipelineViewportStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = &rState.vkViewport,
		.scissorCount = 1,
		.pScissors = &rState.scissorVkRect2D,
	};

	VkExtent2D vkExtent2D
	{
		.width = rPipeline.mInfo.flags & kRenderTarget ? rPipeline.mInfo.vkExtent3D.width : gpGraphics->mFramebufferExtent2D.width,
		.height = rPipeline.mInfo.flags & kRenderTarget ? rPipeline.mInfo.vkExtent3D.height : gpGraphics->mFramebufferExtent2D.height,
	};

	// Negative viewport height (VK_KHR_maintenance1) flips the Vulkan Y axis to match DirectX convention
	rState.vkViewport.x = 0.0f;
	rState.vkViewport.y = static_cast<float>(vkExtent2D.height);
	rState.vkViewport.width = static_cast<float>(vkExtent2D.width);
	rState.vkViewport.height = -static_cast<float>(vkExtent2D.height);

	rState.scissorVkRect2D.extent = vkExtent2D;
}

// Builds the rasterization state: smooth/wide line settings (when supported), wireframe (debug builds),
// cull mode, and depth bias.
static void ConfigureRasterization(GraphicsPipelineState& rState, const Pipeline& rPipeline)
{
	rState.vkPipelineRasterizationLineStateCreateInfoEXT = VkPipelineRasterizationLineStateCreateInfoEXT
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT,
		.pNext = nullptr,
		.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT,
		.stippledLineEnable = VK_FALSE,
		.lineStippleFactor = 0,
		.lineStipplePattern = 0,
	};

	const bool bSmoothLines = (rPipeline.mInfo.flags & PipelineFlags::kLineList) && (gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kSmoothLinesEnabled);
	const bool bWideLines = (rPipeline.mInfo.flags & PipelineFlags::kLineList) && (gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kWideLinesEnabled);

	rState.vkPipelineRasterizationStateCreateInfo = VkPipelineRasterizationStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = bSmoothLines ? &rState.vkPipelineRasterizationLineStateCreateInfoEXT : nullptr,
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
		.lineWidth = bWideLines ? 2.0f : 1.0f,
	};

	if constexpr (kbVulkanWireframe)
	{
		bool bWireframe = gWireframe.Get<bool>();
		if (rPipeline.mInfo.flags & kRenderTarget || rPipeline.mInfo.flags & kNoWireframe)
		{
			bWireframe = false;
		}
		rState.vkPipelineRasterizationStateCreateInfo.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	}
	else
	{
		rState.vkPipelineRasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	}
	rState.vkPipelineRasterizationStateCreateInfo.cullMode = rPipeline.mInfo.flags & kCullBack ? VK_CULL_MODE_BACK_BIT : (rPipeline.mInfo.flags & kCullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE);
	if (rPipeline.mInfo.flags & kDepthBias)
	{
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_TRUE;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = kfDepthBiasConstantFactor;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = kfDepthBiasSlopeFactor;
	}
	else
	{
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		rState.vkPipelineRasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
	}
}

// Builds the multisample state from the global multisampling / sample-shading settings (render targets
// are always single-sample).
static void ConfigureMultisampling(GraphicsPipelineState& rState, const Pipeline& rPipeline)
{
	rState.vkPipelineMultisampleStateCreateInfo = VkPipelineMultisampleStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.rasterizationSamples = rPipeline.mInfo.flags & kRenderTarget ? VK_SAMPLE_COUNT_1_BIT : (gMultisampling.Get<bool>() ? gSampleCount.Get<VkSampleCountFlagBits>() : VK_SAMPLE_COUNT_1_BIT),
		.sampleShadingEnable = (rPipeline.mInfo.flags & kSampleShading && gSampleShading.Get<bool>()) ? VK_TRUE : VK_FALSE,
		.minSampleShading = gMinSampleShading.Get(),
		.pSampleMask = nullptr,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};
}

// Builds the color-blend state: per-attachment blend factors from the blend-mode flags, then replicates
// the attachment across all MRT targets (the lighting pass auto-upgrades a single attachment to 3).
static void ConfigureBlendState(GraphicsPipelineState& rState, const Pipeline& rPipeline)
{
	rState.vkPipelineColorBlendAttachmentState = VkPipelineColorBlendAttachmentState
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
	if (rPipeline.mInfo.flags & kNoColorWrite)
	{
		rState.vkPipelineColorBlendAttachmentState.colorWriteMask = 0;
	}

	rState.vkPipelineColorBlendStateCreateInfo = VkPipelineColorBlendStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &rState.vkPipelineColorBlendAttachmentState,
		.blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
	};

	rState.vkPipelineColorBlendAttachmentState.blendEnable = (rPipeline.mInfo.flags & kAlphaBlend || rPipeline.mInfo.flags & kAdd || rPipeline.mInfo.flags & kAddAlpha || rPipeline.mInfo.flags & kMax) ? VK_TRUE : VK_FALSE;
	rState.vkPipelineColorBlendAttachmentState.colorBlendOp = rPipeline.mInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	rState.vkPipelineColorBlendAttachmentState.alphaBlendOp = rPipeline.mInfo.flags & kMax ? VK_BLEND_OP_MAX : VK_BLEND_OP_ADD;
	if (rPipeline.mInfo.flags & kAlphaBlend)
	{
		rState.vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		rState.vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		rState.vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		rState.vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	}
	else if (rPipeline.mInfo.flags & kAddAlpha)
	{
		rState.vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		rState.vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		rState.vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		rState.vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else
	{
		rState.vkPipelineColorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		rState.vkPipelineColorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		rState.vkPipelineColorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		rState.vkPipelineColorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}

	// Configure MRT blend states
	int32_t iColorAttachmentCount = rPipeline.mInfo.iColorAttachmentCount;
	if (iColorAttachmentCount == 1 && rPipeline.mInfo.vkRenderPass == gpTextureManager->mRenderTargetTextures.mLightingVkRenderPass)
	{
		iColorAttachmentCount = 3;
	}
	ASSERT(iColorAttachmentCount <= kiMaxColorAttachments);
	if (iColorAttachmentCount > 1)
	{
		for (int64_t i = 0; i < iColorAttachmentCount; ++i)
		{
			rState.pMrtBlendStates[i] = rState.vkPipelineColorBlendAttachmentState;
		}
		rState.vkPipelineColorBlendStateCreateInfo.attachmentCount = iColorAttachmentCount;
		rState.vkPipelineColorBlendStateCreateInfo.pAttachments = rState.pMrtBlendStates;
	}
	else
	{
		rState.vkPipelineColorBlendStateCreateInfo.attachmentCount = 1;
		rState.vkPipelineColorBlendStateCreateInfo.pAttachments = &rState.vkPipelineColorBlendAttachmentState;
	}
}

void PipelineCreator::CreateGraphicsPipeline(Pipeline& rPipeline)
{
	ASSERT(rPipeline.mInfo.name.size() > 0);

	// Layout-creation scratch + shader stages stay orchestrator-local; the per-pipeline state structs the
	// create info points at live in the GraphicsPipelineState aggregate below.
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

	// Default-initialized so unmentioned fields are zero; the Configure* stages overwrite their structs.
	GraphicsPipelineState state {};
	state.vkPipelineInputAssemblyStateCreateInfo = VkPipelineInputAssemblyStateCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.topology = rPipeline.mInfo.flags & PipelineFlags::kLineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};
	state.vkPipelineDepthStencilStateCreateInfo = VkPipelineDepthStencilStateCreateInfo
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

	ConfigureVertexInput(state, rPipeline);
	ConfigureViewportScissor(state, rPipeline);
	ConfigureRasterization(state, rPipeline);
	ConfigureMultisampling(state, rPipeline);
	ConfigureBlendState(state, rPipeline);

	state.vkPipelineDepthStencilStateCreateInfo.depthTestEnable = rPipeline.mInfo.flags & kDepthTest ? VK_TRUE : VK_FALSE;
	state.vkPipelineDepthStencilStateCreateInfo.depthWriteEnable = rPipeline.mInfo.flags & kDepthWrite ? VK_TRUE : VK_FALSE;

	VkGraphicsPipelineCreateInfo vkGraphicsPipelineCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stageCount = 2,
		.pStages = pVkPipelineShaderStageCreateInfos,
		.pVertexInputState = &state.vkPipelineVertexInputStateCreateInfo,
		.pInputAssemblyState = &state.vkPipelineInputAssemblyStateCreateInfo,
		.pViewportState = &state.vkPipelineViewportStateCreateInfo,
		.pRasterizationState = &state.vkPipelineRasterizationStateCreateInfo,
		.pMultisampleState = &state.vkPipelineMultisampleStateCreateInfo,
		.pDepthStencilState = &state.vkPipelineDepthStencilStateCreateInfo,
		.pColorBlendState = &state.vkPipelineColorBlendStateCreateInfo,
		.pDynamicState = nullptr,
		// .layout
		// .renderPass
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};

	// Use max of actual count and 3 to handle swapchain recreation scenarios
	size_t uiFramebufferCount = gpSwapchainManager->mFramebuffers.size();
	int64_t iCommandBufferCount = std::max(uiFramebufferCount, static_cast<size_t>(3));
	SetupIndirectBuffer(rPipeline, iCommandBufferCount);

	CreateDescriptorSetLayouts(rPipeline, uniformTextureVkDescriptorSetLayoutCreateInfo, vkPipelineLayoutCreateInfo);

	// Setup pipeline (modules + layout/renderPass resolved after CreateDescriptorSetLayouts)
	pVkPipelineShaderStageCreateInfos[0].module = rPipeline.mInfo.ppShaders[0]->mVkShaderModule;
	pVkPipelineShaderStageCreateInfos[1].module = rPipeline.mInfo.ppShaders[1]->mVkShaderModule;

	vkGraphicsPipelineCreateInfo.layout = rPipeline.mVkPipelineLayout;
	// Default (non-kRenderTarget) scene pipelines render into the F16 HDR intermediate; the resolve pass
	// (itself kRenderTarget with vkRenderPass = mVkRenderPass) writes the swapchain.
	vkGraphicsPipelineCreateInfo.renderPass = rPipeline.mInfo.flags & kRenderTarget ? rPipeline.mInfo.vkRenderPass : gpSwapchainManager->mHdrVkRenderPass;

	CHECK_VK(vkCreateGraphicsPipelines(gpDeviceManager->mVkDevice, gpDeviceManager->mVkPipelineCache, 1, &vkGraphicsPipelineCreateInfo, nullptr, &rPipeline.mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, rPipeline.mVkPipeline, rPipeline.mInfo.name.data());
}

void PipelineCreator::CreateComputePipeline(Pipeline& rPipeline)
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
		// Per-framebuffer host-visible dispatch buffer: the CPU writes the active-region workgroup dims each
		// frame (WriteIndirectComputeBuffer), and RecordComputeIndirect reads slot iCommandBuffer — so size it
		// like the graphics indirect path (SetupIndirectBuffer), not the single-slot device-local branch below.
		size_t uiFramebufferCount = gpSwapchainManager->mFramebuffers.size();
		int64_t iCommandBufferCount = std::max(uiFramebufferCount, static_cast<size_t>(3));
		rPipeline.miIndirectSlotCount = iCommandBufferCount;
		rPipeline.mpIndirectComputeMappedMemory = static_cast<VkDispatchIndirectCommand*>(CreateHostVisibleIndirectBuffer(rPipeline, iCommandBufferCount, sizeof(VkDispatchIndirectCommand)));
	}
	else if (rPipeline.mInfo.flags & kIndirectDeviceLocal)
	{
		// Single-slot dispatch buffer (always read at offset 0); bounds the RecordComputeIndirect index assert
		rPipeline.miIndirectSlotCount = 1;
		Buffer::CreateBuffer(rPipeline.mInfo.name, sizeof(VkDispatchIndirectCommand), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, rPipeline.mIndirectVkBuffer, rPipeline.mIndirectVmaAllocation);
	}

	ASSERT(!(rPipeline.mInfo.flags & kMultiSet)); // Set 2 / multi-material is graphics-only

	Shader* pComputeShader = rPipeline.mInfo.ppShaders[0];

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
		ASSERT(iDescriptorCount < common::ShaderHeader::kiMaxDescriptorSetLayoutBindings);
		pVkDescriptorSetLayoutBindings[iDescriptorCount++] = rBinding;
	}

	// Partition bindings by shader-reflected set index. Bindings on set 1 become the per-pipeline
	// descriptor set; bindings on set 0 are assumed to live on the global Set 0 (compatible types).
	// A compute shader that hasn't been migrated yet declares all bindings without an explicit set,
	// which SPIR-V reports as set 0 — but those bindings won't be type-compatible with the global
	// Set 0 layout (UBO/sampler/sampled-image at fixed slots). For those shaders we keep the legacy
	// single-set layout and detach the global set so binder + writer treat the pipeline as standalone.
	VkDescriptorSetLayoutBinding pVkSet1Bindings[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
	int64_t iSet1Count = 0;
	for (int64_t i = 0; i < iDescriptorCount; ++i)
	{
		uint32_t uiBinding = pVkDescriptorSetLayoutBindings[i].binding;
		uint32_t uiSet = Pipeline::ResolveBindingSetIndex(rPipeline.mInfo, uiBinding);
		if (uiSet == 1)
		{
			pVkSet1Bindings[iSet1Count++] = pVkDescriptorSetLayoutBindings[i];
		}
	}

	if (rPipeline.mVkExternalDescriptorSetLayout != VK_NULL_HANDLE && iSet1Count > 0)
	{
		// Migrated compute pipeline: global Set 0 plus a per-pipeline Set 1.
		uniformTextureVkDescriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(iSet1Count);
		uniformTextureVkDescriptorSetLayoutCreateInfo.pBindings = pVkSet1Bindings;
		VkDescriptorBindingFlags pBindingFlagsSet1[common::ShaderHeader::kiMaxDescriptorSetLayoutBindings] {};
		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfoSet1 {};
		ConfigureUpdateAfterBind(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkSet1Bindings, iSet1Count, pBindingFlagsSet1, bindingFlagsCreateInfoSet1, rPipeline.mInfo.flags & kUpdateAfterBind);
		CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &uniformTextureVkDescriptorSetLayoutCreateInfo, nullptr, &rPipeline.mVkDescriptorSetLayout));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, rPipeline.mVkDescriptorSetLayout, rPipeline.mInfo.name.data());

		VkDescriptorSetLayout pSetLayouts[2] =
		{
			rPipeline.mVkExternalDescriptorSetLayout,
			rPipeline.mVkDescriptorSetLayout,
		};
		VkPushConstantRange vkPushConstantRange {};
		vkPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		vkPushConstantRange.offset = 0;
		vkPushConstantRange.size = ResolvePushConstantBytes(rPipeline);
		vkPipelineLayoutCreateInfo.setLayoutCount = 2;
		vkPipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
		vkPipelineLayoutCreateInfo.pushConstantRangeCount = rPipeline.mInfo.flags & kPushConstants ? 1 : 0;
		vkPipelineLayoutCreateInfo.pPushConstantRanges = rPipeline.mInfo.flags & kPushConstants ? &vkPushConstantRange : nullptr;
		CHECK_VK(vkCreatePipelineLayout(gpDeviceManager->mVkDevice, &vkPipelineLayoutCreateInfo, nullptr, &rPipeline.mVkPipelineLayout));
		VkName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, rPipeline.mVkPipelineLayout, rPipeline.mInfo.name.data());
	}
	else
	{
		// Unmigrated compute (or no global Set 0 available): single-set layout in which the shader's
		// set-0 bindings ARE the per-pipeline set. Null the external-layout pointer so binder/writer
		// treat this pipeline as standalone.
		rPipeline.mVkExternalDescriptorSetLayout = VK_NULL_HANDLE;
		CreateSingleSetPipelineLayout(uniformTextureVkDescriptorSetLayoutCreateInfo, pVkDescriptorSetLayoutBindings, iDescriptorCount, rPipeline, vkPipelineLayoutCreateInfo, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	VkComputePipelineCreateInfo vkComputePipelineCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage =
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = pComputeShader->mVkShaderModule,
			.pName = "main",
		},
		.layout = rPipeline.mVkPipelineLayout,
	};
	CHECK_VK(vkCreateComputePipelines(gpDeviceManager->mVkDevice, gpDeviceManager->mVkPipelineCache, 1, &vkComputePipelineCreateInfo, nullptr, &rPipeline.mVkPipeline));
	VkName(VK_OBJECT_TYPE_PIPELINE, rPipeline.mVkPipeline, rPipeline.mInfo.name.data());
}

} // namespace engine

#endif // defined(BT_CLIENT)
