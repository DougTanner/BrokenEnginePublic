#if defined(BT_CLIENT)

#include "Texture.h"

#include "Ui/GraphicsSettingsWrappersBase.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

namespace
{

struct LayoutMapping
{
	VkImageLayout vkImageLayout;
	VkAccessFlags accessFlags;
	VkPipelineStageFlags stageFlags;
};

static constexpr LayoutMapping kLayoutMappings[]
{
	// kUndefined
	{VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_NONE_KHR, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT},
	// kColorAttachment
	{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
	// kComputeReadOnly
	{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
	// kComputeReadWrite
	{VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
	// kFragmentShaderReadOnly
	{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT},
	// kGeneral
	{VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
	// kShaderReadOnly
	{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT},
	// kTransferDestination
	{VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
	// kTransferSource
	{VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT},
};
static_assert(std::size(kLayoutMappings) == static_cast<size_t>(TextureLayout::kTransferSource) + 1);

} // namespace

VkImageLayout ToVkImageLayout(TextureLayout eLayout)
{
	return kLayoutMappings[static_cast<int>(eLayout)].vkImageLayout;
}

namespace
{

void CreateImageView(VkImage vkImage, const TextureInfo& rInfo, bool bCheckRenderPass, VkImageView& rVkImageView)
{
	VkComponentMapping vkComponentMapping {.r = VK_COMPONENT_SWIZZLE_R, .g = VK_COMPONENT_SWIZZLE_G, .b = VK_COMPONENT_SWIZZLE_B, .a = VK_COMPONENT_SWIZZLE_A};
	if ((!bCheckRenderPass || !(rInfo.textureFlags & kRenderPass)) && rInfo.format == VK_FORMAT_BC4_UNORM_BLOCK)
	{
		vkComponentMapping = {.r = VK_COMPONENT_SWIZZLE_R, .g = VK_COMPONENT_SWIZZLE_R, .b = VK_COMPONENT_SWIZZLE_R, .a = VK_COMPONENT_SWIZZLE_R};
	}
	VkImageViewCreateInfo vkImageViewCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.image = vkImage,
		.viewType = rInfo.viewType,
		.format = rInfo.format,
		.components = vkComponentMapping,
		.subresourceRange = {.aspectMask = rInfo.aspectMask, .baseMipLevel = 0, .levelCount = rInfo.mipLevels, .baseArrayLayer = 0, .layerCount = rInfo.arrayLayers},
	};
	CHECK_VK(vkCreateImageView(gpDeviceManager->mVkDevice, &vkImageViewCreateInfo, nullptr, &rVkImageView));
	VkName(VK_OBJECT_TYPE_IMAGE_VIEW, rVkImageView, rInfo.name.data());
}

} // namespace

void Texture::RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, VkExtent2D vkExtent2D, VkClearColorValue vkClearColorValue, RenderPassFlags_t renderPassFlags, VkSubpassContents vkSubpassContents)
{
	VkClearValue pVkClearValues[] =
	{
		{.color = vkClearColorValue},
		{.depthStencil = {.depth = kfMaxDepth, .stencil = 0}},
		{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
	};

	int64_t iAttachmentCount = 1;
	if (renderPassFlags & RenderPassFlags::kDepth)
	{
		++iAttachmentCount;
	}
	if (renderPassFlags & RenderPassFlags::kMultisampling)
	{
		++iAttachmentCount;
	}

	bool bClear = renderPassFlags & RenderPassFlags::kClear;
	VkRenderPassBeginInfo vkRenderPassBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = vkRenderPass,
		.framebuffer = vkFramebuffer,
		.renderArea =
		{
			.offset = {0, 0},
			.extent = vkExtent2D,
		},
		.clearValueCount = bClear ? static_cast<uint32_t>(iAttachmentCount) : 0,
		.pClearValues = bClear ? pVkClearValues : nullptr,
	};
	vkCmdBeginRenderPass(vkCommandBuffer, &vkRenderPassBeginInfo, vkSubpassContents);
}

void Texture::RecordEndRenderPass(VkCommandBuffer vkCommandBuffer)
{
	vkCmdEndRenderPass(vkCommandBuffer);
}

Texture::Texture(const TextureInfo& rInfo, const std::function<void(void*, int64_t, int64_t)>& rDataFunction)
{
	Create(rInfo, rDataFunction);
}

Texture::~Texture()
{
	Destroy();
}

void Texture::InitDeferred(const TextureInfo& rInfo, VkImageView vkPlaceholderImageView)
{
	mInfo = rInfo;
	mVkImageView = vkPlaceholderImageView;
	// mVkImage stays VK_NULL_HANDLE - no GPU allocation
	// Destroy() early-returns when mVkImage == VK_NULL_HANDLE, so the borrowed placeholder view is never freed
}

void Texture::AdoptTransferredImage(VkImage& rVkImage, VmaAllocation& rVmaAllocation)
{
	Destroy();

	mVkImage = std::exchange(rVkImage, VK_NULL_HANDLE);
	mVmaAllocation = std::exchange(rVmaAllocation, VK_NULL_HANDLE);

	CreateImageView(mVkImage, mInfo, false, mVkImageView);
	++muiGeneration;
}

void Texture::RecordAcquireBarrier(VkCommandBuffer vkCommandBuffer)
{
	bool bQueueFamilyOwnershipTransferOptional = gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kTransferQueueFamilyOwnershipTransferOptional;

	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = bQueueFamilyOwnershipTransferOptional ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = bQueueFamilyOwnershipTransferOptional ? VK_QUEUE_FAMILY_IGNORED : static_cast<uint32_t>(gpInstanceManager->miTransferQueueFamilyIndex),
		.dstQueueFamilyIndex = bQueueFamilyOwnershipTransferOptional ? VK_QUEUE_FAMILY_IGNORED : static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
		.image = mVkImage,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = mInfo.mipLevels, .baseArrayLayer = 0, .layerCount = mInfo.arrayLayers},
	};
	vkCmdPipelineBarrier(vkCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
}

void Texture::Create(const TextureInfo& rInfo, const std::function<void(void*, int64_t, int64_t)>& rDataFunction)
{
	Destroy();

	mInfo = rInfo;

	VkImageCreateInfo vkImageCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = mInfo.flags,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = mInfo.format,
		.extent = mInfo.extent,
		.mipLevels = mInfo.mipLevels,
		.arrayLayers = mInfo.arrayLayers,
		.samples = mInfo.samples,
		.tiling = mInfo.textureFlags & kHostVisible ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
		.usage = mInfo.usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	// Configure VMA allocation
	VmaAllocationCreateInfo vmaAllocationCreateInfo {};
	vmaAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

	if (mInfo.textureFlags & kHostVisible)
	{
		vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	}
	else if (mInfo.textureFlags & kRenderPass)
	{
		// Use dedicated allocations only for large render targets (VMA recommends for resources >32MB or frequently resized)
		static constexpr VkDeviceSize kLargeSizeThreshold = 32 * 1024 * 1024;
		if (common::SizeInBytes(mInfo.format, mInfo.extent.width, mInfo.extent.height) * mInfo.arrayLayers >= kLargeSizeThreshold)
		{
			vmaAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}
	}

	CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &mVkImage, &mVmaAllocation, nullptr));
	VkName(VK_OBJECT_TYPE_IMAGE, mVkImage, mInfo.name.data());

	CreateImageView(mVkImage, mInfo, true, mVkImageView);
	++muiGeneration;

	if (rDataFunction != nullptr)
	{
		UploadImageData(rDataFunction, kUndefined, mInfo.eTextureLayout);
	}

	if (mInfo.textureFlags & kRenderPass)
	{
		CreateRenderTarget();
	}

	// Transition to final layout only if no rDataFunction was provided (rDataFunction case handles this above)
	if (rDataFunction == nullptr && mInfo.eTextureLayout != kUndefined)
	{
		OneShotCommandBuffer oneShotCommandBuffer;
		TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kUndefined, mInfo.eTextureLayout);
		oneShotCommandBuffer.Execute();
	}
}

void Texture::UpdateData(const std::function<void(void*, int64_t, int64_t)>& rDataFunction)
{
	ASSERT(mVkImage != VK_NULL_HANDLE);
	UploadImageData(rDataFunction, kShaderReadOnly, kShaderReadOnly);
}

void Texture::UploadImageData(const std::function<void(void*, int64_t, int64_t)>& rDataFunction, TextureLayout eOldLayout, TextureLayout eFinalLayout)
{
	// Calculate total buffer size for all mip levels
	VkDeviceSize vkDeviceSize = common::ComputeImageByteSize(mInfo.format, mInfo.extent.width, mInfo.extent.height, mInfo.mipLevels, mInfo.arrayLayers, mInfo.extent.depth);

	// Create staging buffer and fill with data
	StagingBuffer stagingBuffer(mInfo.name, vkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	// Use VMA's pre-mapped pointer
	rDataFunction(stagingBuffer.vmaAllocationInfo.pMappedData, 0, vkDeviceSize);

	// Record all operations into a single command buffer
	OneShotCommandBuffer oneShotCommandBuffer;

	// Transition layout to transfer destination
	TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, eOldLayout, kTransferDestination);

	// Record all buffer-to-image copies
	size_t uiOffset = 0;
	for (uint32_t i = 0; i < mInfo.arrayLayers; ++i)
	{
		uint32_t uiWidth = mInfo.extent.width;
		uint32_t uiHeight = mInfo.extent.height;
		for (uint32_t uiLevel = 0; uiLevel < mInfo.mipLevels; ++uiLevel)
		{
			VkBufferImageCopy vkBufferImageCopy {};
			vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vkBufferImageCopy.imageSubresource.mipLevel = uiLevel;
			vkBufferImageCopy.imageSubresource.baseArrayLayer = i;
			vkBufferImageCopy.imageSubresource.layerCount = 1;
			vkBufferImageCopy.imageExtent.width = uiWidth;
			vkBufferImageCopy.imageExtent.height = uiHeight;
			vkBufferImageCopy.imageExtent.depth = 1;
			vkBufferImageCopy.bufferOffset = uiOffset;

			uiOffset += common::SizeInBytes(mInfo.format, uiWidth, uiHeight);
			uiWidth = std::max(1u, uiWidth / 2);
			uiHeight = std::max(1u, uiHeight / 2);

			vkCmdCopyBufferToImage(oneShotCommandBuffer.mVkCommandBuffer, stagingBuffer.vkBuffer, mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);
		}
	}

	// Transition to final layout if needed
	if (eFinalLayout != kUndefined)
	{
		TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, eFinalLayout);
	}

	oneShotCommandBuffer.Execute();
}

void Texture::CreateRenderTarget()
{
	if (mInfo.textureFlags & kDepth)
	{
		VkImageAspectFlags vkImageAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (gpInstanceManager->mDepthVkFormat == VK_FORMAT_D16_UNORM_S8_UINT || gpInstanceManager->mDepthVkFormat == VK_FORMAT_D24_UNORM_S8_UINT || gpInstanceManager->mDepthVkFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
		{
			vkImageAspectFlags |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		mpDepthTexture = std::make_unique<Texture>();
		mpDepthTexture->Create(TextureInfo
		{
			.textureFlags = {},
			.name = "Depth",
			.flags = 0,
			.format = gpInstanceManager->mDepthVkFormat,
			.extent = VkExtent3D {.width = mInfo.extent.width, .height = mInfo.extent.height, .depth = 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = vkImageAspectFlags,
			.eTextureLayout = kUndefined,
		});
	}

	VkAttachmentDescription pVkAttachmentDescriptions[]
	{
		VkAttachmentDescription
		{
			.flags = 0,
			.format = mInfo.format,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = mInfo.renderPassVkAttachmentLoadOp,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = mInfo.renderPassInitialVkImageLayout,
			.finalLayout = mInfo.renderPassFinalVkImageLayout,
		},
		// Depth
		// This optional kDepth branch reuses a per-texture depth image every frame (UNDEFINED initial layout + CLEAR
		// loadOp), but the render pass built here has only an OUTGOING (0 -> EXTERNAL) dependency; there is currently
		// NO live TextureFlags::kDepth caller, so the incoming EXTERNAL -> 0 dependency is left implicit. A future
		// kDepth user MUST add an explicit incoming dependency with depth src stage + access scopes
		// (VK_PIPELINE_STAGE_EARLY/LATE_FRAGMENT_TESTS_BIT + VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, mirroring
		// the HDR pass's incoming depth dependency in SwapchainManager::CreateRenderPass) to avoid the same latent depth write-after-write.
		VkAttachmentDescription
		{
			.flags = 0,
			.format = gpInstanceManager->mDepthVkFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};
	VkAttachmentReference vkAttachmentReference
	{
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference depthVkAttachmentReference
	{
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};
	VkSubpassDescription vkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &vkAttachmentReference,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = mInfo.textureFlags & kDepth ? &depthVkAttachmentReference : nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};
	// This VK_SUBPASS_EXTERNAL subpass dependency, combined with mInfo.renderPassFinalVkImageLayout, will insert an implicit pipeline barrier at the end of the render pass
	VkSubpassDependency vkSubpassDependency
	{
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = mInfo.renderPassDstStageMask,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = mInfo.renderPassDstAccessMask,
		.dependencyFlags = 0,
	};
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = mInfo.textureFlags & kDepth ? 2u : 1u,
		.pAttachments = pVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &vkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mVkRenderPass, mInfo.name.data());
	VkImageView pVkImageViews[] {mVkImageView, mInfo.textureFlags & kDepth ? mpDepthTexture->mVkImageView : nullptr};
	VkFramebufferCreateInfo vkFramebufferCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderPass = mVkRenderPass,
		.attachmentCount = mInfo.textureFlags & kDepth ? 2u : 1u,
		.pAttachments = pVkImageViews,
		.width = mInfo.extent.width,
		.height = mInfo.extent.height,
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &mVkFramebuffer));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mVkFramebuffer, mInfo.name.data());
}

void Texture::Destroy() noexcept
{
	if (mVkImage == VK_NULL_HANDLE)
	{
		return;
	}

	vkDestroyImageView(gpDeviceManager->mVkDevice, mVkImageView, nullptr);
	mVkImageView = VK_NULL_HANDLE;

	vmaDestroyImage(gpDeviceManager->mpAllocator, mVkImage, mVmaAllocation);
	mVkImage = VK_NULL_HANDLE;
	mVmaAllocation = VK_NULL_HANDLE;

	if (mVkRenderPass != VK_NULL_HANDLE)
	{
		mpDepthTexture.reset();

		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mVkFramebuffer, nullptr);
		mVkFramebuffer = VK_NULL_HANDLE;
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mVkRenderPass, nullptr);
		mVkRenderPass = VK_NULL_HANDLE;
	}
}

void Texture::FreeGpuResources() noexcept
{
	// Tears down image + view but preserves mInfo so AdoptTransferredImage can re-attach fresh
	// GPU resources later. Terrain textures have no render-pass / framebuffer, so Destroy() is
	// equivalent here; the named alias documents intent at the call site (LRU eviction).
	Destroy();
}

void Texture::TransitionImageLayout(VkCommandBuffer vkCommandBuffer, TextureLayout eOldLayout, TextureLayout eNewLayout)
{
	const LayoutMapping& rSrc = kLayoutMappings[static_cast<int>(eOldLayout)];
	const LayoutMapping& rDst = kLayoutMappings[static_cast<int>(eNewLayout)];

	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = rSrc.accessFlags,
		.dstAccessMask = rDst.accessFlags,
		.oldLayout = rSrc.vkImageLayout,
		.newLayout = rDst.vkImageLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = mVkImage,
		.subresourceRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mInfo.mipLevels,
			.baseArrayLayer = 0,
			.layerCount = mInfo.arrayLayers,
		},
	};

	vkCmdPipelineBarrier(vkCommandBuffer, rSrc.stageFlags, rDst.stageFlags, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
}

void Texture::RecordCopyImageFrom(VkCommandBuffer vkCommandBuffer, const Texture& rSource)
{
	VkImageCopy vkImageCopy
	{
		.srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
		.srcOffset = {0, 0, 0},
		.dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
		.dstOffset = {0, 0, 0},
		.extent = mInfo.extent,
	};
	vkCmdCopyImage(vkCommandBuffer, rSource.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkImageCopy);
}

void Texture::RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer)
{
	RenderPassFlags_t renderPassFlags;
	if (mInfo.textureFlags & TextureFlags::kDepth)
	{
		renderPassFlags.Set(RenderPassFlags::kDepth);
	}
	if (mInfo.textureFlags & TextureFlags::kMultisampling && gMultisampling.Get<bool>())
	{
		renderPassFlags.Set(RenderPassFlags::kMultisampling);
	}
	if (mInfo.renderPassVkAttachmentLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
	{
		renderPassFlags.Set(RenderPassFlags::kClear);
	}
	RecordBeginRenderPass(vkCommandBuffer, mVkRenderPass, mVkFramebuffer, {mInfo.extent.width, mInfo.extent.height}, mInfo.renderPassVkClearColorValue, renderPassFlags);
}

} // namespace engine

#endif // defined(BT_CLIENT)
