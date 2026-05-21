#if defined(BT_CLIENT)

#include "RenderTargetTextures.h"

#include "InstanceManager.h"
#include "DeviceManager.h"
#include "Graphics/Graphics.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

void RenderTargetTextures::DestroyWaterSkyboxOneTextures()
{
	if (mWaterSkyboxOneVkFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mWaterSkyboxOneVkFramebuffer, nullptr);
		mWaterSkyboxOneVkFramebuffer = VK_NULL_HANDLE;
	}
	if (mWaterSkyboxOneVkRenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mWaterSkyboxOneVkRenderPass, nullptr);
		mWaterSkyboxOneVkRenderPass = VK_NULL_HANDLE;
	}
	mWaterSkyboxOneResolveTexture.Destroy();
	mWaterSkyboxOneMsaaTexture.Destroy();
}

void RenderTargetTextures::CreateWaterSkyboxOneTextures()
{
	DestroyWaterSkyboxOneTextures();

	// Hardcoded 4x MSAA clamped to device max. The matching pipeline (kPipelineWaterSkyboxOne) reads
	// the same clamp in PipelineCreator.cpp via meMaxMultisampleCount. When the device caps below 2x,
	// MSAA is effectively disabled and the pass collapses to a single-attachment passthrough — the
	// resolve texture becomes the sole color attachment and the MSAA sibling is not allocated.
	VkSampleCountFlagBits eSamples = std::min(VK_SAMPLE_COUNT_4_BIT, gpInstanceManager->meMaxMultisampleCount);
	bool bHasMsaa = eSamples > VK_SAMPLE_COUNT_1_BIT;

	float fMultiplier = gWaterSkyboxOneRenderMultiplier.Get();
	uint32_t uiWidth = std::max(1u, static_cast<uint32_t>(static_cast<float>(gpGraphics->mFramebufferExtent2D.width) * fMultiplier));
	uint32_t uiHeight = std::max(1u, static_cast<uint32_t>(static_cast<float>(gpGraphics->mFramebufferExtent2D.height) * fMultiplier));
	VkFormat eFormat = gpInstanceManager->mFramebufferVkFormat;

	if (bHasMsaa)
	{
		// MSAA color attachment — transient, never sampled. Post-create layout is kColorAttachment
		// (not kShaderReadOnly) because the image has no SAMPLED_BIT usage; kShaderReadOnly transitions
		// would violate VUID-VkImageMemoryBarrier-oldLayout-01211. The render pass itself transitions
		// from UNDEFINED on every begin (loadOp=CLEAR discards prior contents).
		mWaterSkyboxOneMsaaTexture.Create(TextureInfo
		{
			.textureFlags = {},
			.name = "WaterSkyboxOneMsaa",
			.flags = 0,
			.format = eFormat,
			.extent = VkExtent3D {uiWidth, uiHeight, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = eSamples,
			.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kColorAttachment,
		});
	}

	// Single-sample resolve target — sampled by the main water pipeline at binding 12. Also acts as
	// the sole color attachment when MSAA is disabled.
	mWaterSkyboxOneResolveTexture.Create(TextureInfo
	{
		.textureFlags = {},
		.name = "WaterSkyboxOneResolve",
		.flags = 0,
		.format = eFormat,
		.extent = VkExtent3D {uiWidth, uiHeight, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});

	VkAttachmentDescription pVkAttachmentDescriptions[2]
	{
		// Attachment 0: MSAA (or single-sample when MSAA disabled) — store discarded; we keep the resolved version.
		VkAttachmentDescription
		{
			.flags = 0,
			.format = eFormat,
			.samples = eSamples,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = bHasMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = bHasMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		// Attachment 1: single-sample resolve target — finalLayout matches the layout the main water
		// shader needs at sample-time, so no manual barrier is required.
		VkAttachmentDescription
		{
			.flags = 0,
			.format = eFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
	};
	VkAttachmentReference vkColorRef {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference vkResolveRef {.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription vkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &vkColorRef,
		.pResolveAttachments = bHasMsaa ? &vkResolveRef : nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};
	VkSubpassDependency vkSubpassDependency
	{
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dependencyFlags = 0,
	};
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = bHasMsaa ? 2u : 1u,
		.pAttachments = pVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &vkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mWaterSkyboxOneVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mWaterSkyboxOneVkRenderPass, "WaterSkyboxOne");

	// Framebuffer: attachment 0 is the MSAA image when 4x is active, otherwise the resolve image
	// (single-attachment passthrough). Attachment 1 (the resolve) is only bound when MSAA is active.
	VkImageView pVkImageViews[2]
	{
		bHasMsaa ? mWaterSkyboxOneMsaaTexture.mVkImageView : mWaterSkyboxOneResolveTexture.mVkImageView,
		mWaterSkyboxOneResolveTexture.mVkImageView,
	};
	VkFramebufferCreateInfo vkFramebufferCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderPass = mWaterSkyboxOneVkRenderPass,
		.attachmentCount = bHasMsaa ? 2u : 1u,
		.pAttachments = pVkImageViews,
		.width = uiWidth,
		.height = uiHeight,
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &mWaterSkyboxOneVkFramebuffer));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mWaterSkyboxOneVkFramebuffer, "WaterSkyboxOne");
}

} // namespace engine

#endif // defined(BT_CLIENT)
