#include "Texture.h"

#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

void Texture::RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer, VkRenderPass vkRenderPass, VkFramebuffer vkFramebuffer, VkExtent2D vkExtent2D, VkClearColorValue vkClearColorValue, bool bDepth, bool bMultisampling, bool bClear)
{
	VkClearValue pVkClearValues[] =
	{
		{.color = vkClearColorValue},
		{.depthStencil = {.depth = kfMaxDepth, .stencil = 0}},
		{.color = {0.0f, 0.0f, 0.0f, 0.0f}},
	};

	int64_t iAttachmentCount = 1;
	if (bDepth)
	{
		++iAttachmentCount;
	}
	if (bMultisampling)
	{
		++iAttachmentCount;
	}

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
	vkCmdBeginRenderPass(vkCommandBuffer, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Texture::RecordEndRenderPass(VkCommandBuffer vkCommandBuffer, [[maybe_unused]] VkRenderPass vkRenderPass)
{
	vkCmdEndRenderPass(vkCommandBuffer);
}

Texture::Texture(const TextureInfo& rInfo, std::function<void(void*,int64_t,int64_t)> dataFunction)
{
	Create(rInfo, dataFunction);
}

Texture::~Texture()
{
	Destroy();
}

void Texture::ReCreate()
{
	ASSERT(mInfo.extent.width > 0 && mInfo.extent.height > 0);

	Destroy();
	Create(mInfo, nullptr);
}

void Texture::Create(const TextureInfo& rInfo, std::function<void(void*, int64_t, int64_t)> dataFunction)
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
	VmaAllocationCreateInfo vmaAllocationCreateInfo = {};
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

	VmaAllocationInfo vmaAllocationInfo {};
	CHECK_VK(vmaCreateImage(gpDeviceManager->mpAllocator, &vkImageCreateInfo, &vmaAllocationCreateInfo, &mVkImage, &mVmaAllocation, &vmaAllocationInfo));
	VK_NAME(VK_OBJECT_TYPE_IMAGE, mVkImage, mInfo.pcName.data());

	// Get the VkDeviceMemory for compatibility with existing code
	mVkDeviceMemory = vmaAllocationInfo.deviceMemory;

	VkComponentMapping vkComponentMapping = {.r = VK_COMPONENT_SWIZZLE_R, .g = VK_COMPONENT_SWIZZLE_G, .b = VK_COMPONENT_SWIZZLE_B, .a = VK_COMPONENT_SWIZZLE_A};
	if (!(mInfo.textureFlags & kRenderPass) && mInfo.format == VK_FORMAT_BC4_UNORM_BLOCK)
	{
		vkComponentMapping = {.r = VK_COMPONENT_SWIZZLE_R, .g = VK_COMPONENT_SWIZZLE_R, .b = VK_COMPONENT_SWIZZLE_R, .a = VK_COMPONENT_SWIZZLE_R};
	}
	VkImageViewCreateInfo vkImageViewCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.image = mVkImage,
		.viewType = mInfo.viewType,
		.format = mInfo.format,
		.components = vkComponentMapping,
		.subresourceRange = {.aspectMask = mInfo.aspectMask, .baseMipLevel = 0, .levelCount = mInfo.mipLevels, .baseArrayLayer = 0, .layerCount = mInfo.arrayLayers},
	};
	CHECK_VK(vkCreateImageView(gpDeviceManager->mVkDevice, &vkImageViewCreateInfo, nullptr, &mVkImageView));
	VK_NAME(VK_OBJECT_TYPE_IMAGE_VIEW, mVkImageView, mInfo.pcName.data());

	if (dataFunction != nullptr)
	{
		OneShotCommandBuffer oneShotCommandBufferTransition;
		TransitionImageLayout(oneShotCommandBufferTransition.mVkCommandBuffer, kUndefined, kTransferDestination);
		oneShotCommandBufferTransition.Execute(true);

		VkDeviceSize vkDeviceSize = 0;
		uint32_t uiWidth = mInfo.extent.width;
		uint32_t uiHeight = mInfo.extent.height;
		for (uint32_t i = 0; i < mInfo.mipLevels; i++)
		{
			vkDeviceSize += mInfo.arrayLayers * mInfo.extent.depth * common::SizeInBytes(mInfo.format, uiWidth, uiHeight);
			uiWidth /= 2;
			uiHeight /= 2;
		}

		VkBuffer stagingVkBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingVkDeviceMemory = VK_NULL_HANDLE;
		VmaAllocation stagingVmaAllocation = VK_NULL_HANDLE;
		VmaAllocationInfo stagingVmaAllocationInfo {};
		Buffer::CreateBuffer(mInfo.pcName, vkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVkBuffer, stagingVkDeviceMemory, stagingVmaAllocation, &stagingVmaAllocationInfo);

		// Use VMA's pre-mapped pointer
		dataFunction(stagingVmaAllocationInfo.pMappedData, 0, vkDeviceSize);

		// Batch all buffer-to-image copies into a single command buffer for performance
		OneShotCommandBuffer oneShotCommandBuffer;
		size_t uiOffset = 0;
		for (uint32_t i = 0; i < mInfo.arrayLayers; i++)
		{
			uiWidth = mInfo.extent.width;
			uiHeight = mInfo.extent.height;
			for (uint32_t level = 0; level < mInfo.mipLevels; level++)
			{
				VkBufferImageCopy vkBufferImageCopy = {};
				vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				vkBufferImageCopy.imageSubresource.mipLevel = level;
				vkBufferImageCopy.imageSubresource.baseArrayLayer = i;
				vkBufferImageCopy.imageSubresource.layerCount = 1;
				vkBufferImageCopy.imageExtent.width = uiWidth;
				vkBufferImageCopy.imageExtent.height = uiHeight;
				vkBufferImageCopy.imageExtent.depth = 1;
				vkBufferImageCopy.bufferOffset = uiOffset;

				uiOffset += common::SizeInBytes(mInfo.format, uiWidth, uiHeight);
				uiWidth /= 2;
				uiHeight /= 2;

				vkCmdCopyBufferToImage(oneShotCommandBuffer.mVkCommandBuffer, stagingVkBuffer, mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);
			}
		}
		oneShotCommandBuffer.Execute(true);

		// Cleanup
		vmaDestroyBuffer(gpDeviceManager->mpAllocator, stagingVkBuffer, stagingVmaAllocation);
	}

	if (mInfo.textureFlags & kRenderPass)
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
				.pcName = "Depth",
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
			vkSubpassDescription.flags = 0,
			vkSubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkSubpassDescription.inputAttachmentCount = 0,
			vkSubpassDescription.pInputAttachments = nullptr,
			vkSubpassDescription.colorAttachmentCount = 1,
			vkSubpassDescription.pColorAttachments = &vkAttachmentReference,
			vkSubpassDescription.pResolveAttachments = nullptr,
			vkSubpassDescription.pDepthStencilAttachment = mInfo.textureFlags & kDepth ? &depthVkAttachmentReference : nullptr,
			vkSubpassDescription.preserveAttachmentCount = 0,
			vkSubpassDescription.pPreserveAttachments = nullptr,
		};
		// This VK_SUBPASS_EXTERNAL subpass dependency, combined with mInfo.renderPassFinalVkImageLayout, will insert an implicit pipeline barrier at the end of the render pass
		VkSubpassDependency vkSubpassDependency =
		{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
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
		VK_NAME(VK_OBJECT_TYPE_RENDER_PASS, mVkRenderPass, mInfo.pcName.data());
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
		VK_NAME(VK_OBJECT_TYPE_FRAMEBUFFER, mVkFramebuffer, mInfo.pcName.data());
	}

	if (mInfo.eTextureLayout != kUndefined)
	{
		OneShotCommandBuffer oneShotCommandBuffer;
		TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, dataFunction ? kTransferDestination : kUndefined, mInfo.eTextureLayout);
		oneShotCommandBuffer.Execute(true);
	}
}

// Update texture data in-place using staging buffer
void Texture::UpdateData(std::function<void(void*, int64_t, int64_t)> dataFunction)
{
	ASSERT(mVkImage != VK_NULL_HANDLE);
	ASSERT(dataFunction != nullptr);

	// Transition to transfer destination layout
	OneShotCommandBuffer oneShotCommandBufferTransition;
	TransitionImageLayout(oneShotCommandBufferTransition.mVkCommandBuffer, kShaderReadOnly, kTransferDestination);
	oneShotCommandBufferTransition.Execute(true);

	// Calculate total buffer size for all mip levels
	VkDeviceSize vkDeviceSize = 0;
	uint32_t uiWidth = mInfo.extent.width;
	uint32_t uiHeight = mInfo.extent.height;
	for (uint32_t i = 0; i < mInfo.mipLevels; i++)
	{
		vkDeviceSize += mInfo.arrayLayers * mInfo.extent.depth * common::SizeInBytes(mInfo.format, uiWidth, uiHeight);
		uiWidth /= 2;
		uiHeight /= 2;
	}

	// Create staging buffer and copy data
	VkBuffer stagingVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingVkDeviceMemory = VK_NULL_HANDLE;
	VmaAllocation stagingVmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo stagingVmaAllocationInfo {};
	Buffer::CreateBuffer(mInfo.pcName, vkDeviceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVkBuffer, stagingVkDeviceMemory, stagingVmaAllocation, &stagingVmaAllocationInfo);

	// Use VMA's pre-mapped pointer
	dataFunction(stagingVmaAllocationInfo.pMappedData, 0, vkDeviceSize);

	// Batch all buffer-to-image copies into a single command buffer for performance
	OneShotCommandBuffer oneShotCommandBuffer;
	size_t uiOffset = 0;
	for (uint32_t i = 0; i < mInfo.arrayLayers; i++)
	{
		uiWidth = mInfo.extent.width;
		uiHeight = mInfo.extent.height;
		for (uint32_t level = 0; level < mInfo.mipLevels; level++)
		{
			VkBufferImageCopy vkBufferImageCopy = {};
			vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			vkBufferImageCopy.imageSubresource.mipLevel = level;
			vkBufferImageCopy.imageSubresource.baseArrayLayer = i;
			vkBufferImageCopy.imageSubresource.layerCount = 1;
			vkBufferImageCopy.imageExtent.width = uiWidth;
			vkBufferImageCopy.imageExtent.height = uiHeight;
			vkBufferImageCopy.imageExtent.depth = 1;
			vkBufferImageCopy.bufferOffset = uiOffset;

			uiOffset += common::SizeInBytes(mInfo.format, uiWidth, uiHeight);
			uiWidth /= 2;
			uiHeight /= 2;

			vkCmdCopyBufferToImage(oneShotCommandBuffer.mVkCommandBuffer, stagingVkBuffer, mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkBufferImageCopy);
		}
	}
	oneShotCommandBuffer.Execute(true);

	// Cleanup staging buffer
	vmaDestroyBuffer(gpDeviceManager->mpAllocator, stagingVkBuffer, stagingVmaAllocation);

	// Transition back to shader read only layout
	OneShotCommandBuffer oneShotCommandBufferFinal;
	TransitionImageLayout(oneShotCommandBufferFinal.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	oneShotCommandBufferFinal.Execute(true);
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
	mVkDeviceMemory = VK_NULL_HANDLE;

	if (mVkRenderPass != VK_NULL_HANDLE)
	{
		mpDepthTexture.reset();

		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mVkFramebuffer, nullptr);
		mVkFramebuffer = VK_NULL_HANDLE;
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mVkRenderPass, nullptr);
		mVkRenderPass = VK_NULL_HANDLE;
	}
}

void Texture::TransitionImageLayout(VkCommandBuffer vkCommandBuffer, TextureLayout eOldLayout, TextureLayout eNewLayout)
{
	VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAccessFlags srcAccessMask = VK_ACCESS_NONE_KHR;
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	switch (eOldLayout)
	{
		case kUndefined:
			break;

		case kColorAttachment:
			oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;

		case kComputeReadOnly:
			oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kComputeReadWrite:
			oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kShaderReadOnly:
			oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kTransferDestination:
			oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;

		default:
			throw std::runtime_error("Unhandled case in TransitionImageLayout()");
	}

	VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAccessFlags dstAccessMask = VK_ACCESS_NONE_KHR;
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	switch (eNewLayout)
	{
		case kColorAttachment:
			newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;

		case kComputeReadOnly:
			newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kComputeReadWrite:
			newLayout = VK_IMAGE_LAYOUT_GENERAL;
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kGeneral:
			newLayout = VK_IMAGE_LAYOUT_GENERAL;
			dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;

		case kShaderReadOnly:
			newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			break;

		case kTransferDestination:
			newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;

		default:
			throw std::runtime_error("Unhandled case in TransitionImageLayout()");
	}

	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = srcAccessMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
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

	vkCmdPipelineBarrier(vkCommandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);
}

void Texture::RecordBeginRenderPass(VkCommandBuffer vkCommandBuffer)
{
	RecordBeginRenderPass(vkCommandBuffer, mVkRenderPass, mVkFramebuffer, {mInfo.extent.width, mInfo.extent.height}, mInfo.renderPassVkClearColorValue, mInfo.textureFlags & kDepth, mInfo.textureFlags & kMultisampling && gMultisampling.Get<bool>(), mInfo.renderPassVkAttachmentLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
}

void Texture::RecordEndRenderPass(VkCommandBuffer vkCommandBuffer)
{
	RecordEndRenderPass(vkCommandBuffer, mVkRenderPass);
}

} // namespace engine
