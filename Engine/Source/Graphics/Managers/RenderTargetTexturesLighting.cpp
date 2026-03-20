#if defined(BT_CLIENT)

#include "RenderTargetTextures.h"

#include "TextureManager.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

std::tuple<int64_t, int64_t> CombineTextureInfo()
{
	int64_t iCombineTextureIndex = static_cast<int64_t>(gLightingCombineIndex.Get());
	int64_t iBlurTextureCount = gpTextureManager->mRenderTargetTextures.miLightingBlurCount - iCombineTextureIndex - 1;
	return std::make_tuple(iCombineTextureIndex, iBlurTextureCount);
}

void RenderTargetTextures::DestroyLightingTextures()
{
	for (int64_t i = 0; i < shaders::kiMaxLightingBlurCount; ++i)
	{
		if (mpLightingBlurVkFramebuffers[i] != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mpLightingBlurVkFramebuffers[i], nullptr);
			mpLightingBlurVkFramebuffers[i] = VK_NULL_HANDLE;
		}
		if (mpLightingBlurVkRenderPasses[i] != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(gpDeviceManager->mVkDevice, mpLightingBlurVkRenderPasses[i], nullptr);
			mpLightingBlurVkRenderPasses[i] = VK_NULL_HANDLE;
		}
	}

	if (mLightingVkFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mLightingVkFramebuffer, nullptr);
		mLightingVkFramebuffer = VK_NULL_HANDLE;
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mLightingVkRenderPass, nullptr);
		mLightingVkRenderPass = VK_NULL_HANDLE;
	}
}

void RenderTargetTextures::CreateLightingTextures()
{
	DestroyLightingTextures();

	auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingTextureMultiplier.Get());
	// Create 3 lighting textures without individual render passes
	TextureInfo lightingTextureInfo
	{
		.textureFlags = {},
		.name = "RedLighting",
		.flags = 0,
		.format = shaders::keLightingFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iLightingTextureX), static_cast<uint32_t>(iLightingTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	};
	mpLightingTextures[0].Create(lightingTextureInfo);
	lightingTextureInfo.name = "GreenLighting";
	mpLightingTextures[1].Create(lightingTextureInfo);
	lightingTextureInfo.name = "BlueLighting";
	mpLightingTextures[2].Create(lightingTextureInfo);

	// Create MRT render pass with 3 color attachments
	VkAttachmentDescription pVkAttachmentDescriptions[3]
	{
		VkAttachmentDescription
		{
			.flags = 0,
			.format = shaders::keLightingFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkAttachmentDescription
		{
			.flags = 0,
			.format = shaders::keLightingFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		VkAttachmentDescription
		{
			.flags = 0,
			.format = shaders::keLightingFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
	};
	VkAttachmentReference pVkAttachmentReferences[3]
	{
		{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		{.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		{.attachment = 2, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	};
	VkSubpassDescription vkSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 3,
		.pColorAttachments = pVkAttachmentReferences,
		.pResolveAttachments = nullptr,
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
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dependencyFlags = 0,
	};
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = 3,
		.pAttachments = pVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = 1,
		.pDependencies = &vkSubpassDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkRenderPassCreateInfo, nullptr, &mLightingVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mLightingVkRenderPass, "LightingMRT");

	// Create framebuffer binding all 3 lighting textures
	VkImageView pVkImageViews[3] {mpLightingTextures[0].mVkImageView, mpLightingTextures[1].mVkImageView, mpLightingTextures[2].mVkImageView};
	VkFramebufferCreateInfo vkFramebufferCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderPass = mLightingVkRenderPass,
		.attachmentCount = 3,
		.pAttachments = pVkImageViews,
		.width = static_cast<uint32_t>(iLightingTextureX),
		.height = static_cast<uint32_t>(iLightingTextureY),
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &mLightingVkFramebuffer));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mLightingVkFramebuffer, "LightingMRT");

	float fDownscale = gLightingBlurDownscale.Get();
	CreateBlurTextures(iLightingTextureX, iLightingTextureY, fDownscale, shaders::kiMaxLightingBlurCount);

	for (int64_t i = 0; i < miLightingBlurCount; ++i)
	{
		CreateBlurRenderPassAndFramebuffer(i, mpRedLightingBlurTextures[i].mInfo.extent.width, mpRedLightingBlurTextures[i].mInfo.extent.height);
	}

	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	mppLightingFinalTextures[0] = &mpRedLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[1] = &mpGreenLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[2] = &mpBlueLightingBlurTextures[iCombineTextureIndex];
}

void RenderTargetTextures::CreateBlurTextures(int64_t iLightingTextureX, int64_t iLightingTextureY, float fDownscale, int64_t iMaxCount)
{
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	int64_t iLightingBlurTextureX = iLightingTextureX;
	int64_t iLightingBlurTextureY = iLightingTextureY;
	miLightingBlurCount = 0;
	for (int64_t i = 0; i < iMaxCount; ++i)
	{
		iLightingBlurTextureX = static_cast<int64_t>(fDownscale * static_cast<float>(iLightingBlurTextureX));
		iLightingBlurTextureX = std::max(1ll, iLightingBlurTextureX);
		iLightingBlurTextureY = static_cast<int64_t>(fDownscale * static_cast<float>(iLightingBlurTextureY));
		iLightingBlurTextureY = std::max(1ll, iLightingBlurTextureY);

		if (iLightingBlurTextureX > 2 && iLightingBlurTextureY > 2)
		{
			++miLightingBlurCount;
		}

		TextureInfo lightingBlurTextureInfo
		{
			.textureFlags = {kRenderPass},
			.name = "RedLightingBlur",
			.flags = 0,
			.format = shaders::keLightingFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(iLightingBlurTextureX), static_cast<uint32_t>(iLightingBlurTextureY), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.renderPassVkAttachmentLoadOp = i == iCombineTextureIndex ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.renderPassVkClearColorValue = {0.0f, 0.0f, 0.0f, 0.0f},
			.eTextureLayout = kFragmentShaderReadOnly,
		};

		mpRedLightingBlurTextures[i].Create(lightingBlurTextureInfo);
		lightingBlurTextureInfo.name = "GreenLightingBlur";
		mpGreenLightingBlurTextures[i].Create(lightingBlurTextureInfo);
		lightingBlurTextureInfo.name = "BlueLightingBlur";
		mpBlueLightingBlurTextures[i].Create(lightingBlurTextureInfo);
	}
	Log("kiMaxLightingBlurCount: {} -> miLightingBlurCount: {}", iMaxCount, miLightingBlurCount);
}

void RenderTargetTextures::CreateBlurRenderPassAndFramebuffer(int64_t iLevel, int64_t iBlurTextureX, int64_t iBlurTextureY)
{
	VkAttachmentDescription pBlurAttachments[3] {};
	for (int64_t j = 0; j < 3; ++j)
	{
		pBlurAttachments[j] =
		{
			.flags = 0,
			.format = shaders::keLightingFormat,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
	}
	VkAttachmentReference pBlurAttachmentRefs[3]
	{
		{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		{.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
		{.attachment = 2, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	};
	VkSubpassDescription vkBlurSubpass
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 3,
		.pColorAttachments = pBlurAttachmentRefs,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};
	VkSubpassDependency vkBlurDependency
	{
		.srcSubpass = 0,
		.dstSubpass = VK_SUBPASS_EXTERNAL,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dependencyFlags = 0,
	};
	VkRenderPassCreateInfo vkBlurRenderPassInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = 3,
		.pAttachments = pBlurAttachments,
		.subpassCount = 1,
		.pSubpasses = &vkBlurSubpass,
		.dependencyCount = 1,
		.pDependencies = &vkBlurDependency,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkBlurRenderPassInfo, nullptr, &mpLightingBlurVkRenderPasses[iLevel]));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mpLightingBlurVkRenderPasses[iLevel], "LightingBlurMRT");

	VkImageView pBlurImageViews[3]
	{
		mpRedLightingBlurTextures[iLevel].mVkImageView,
		mpGreenLightingBlurTextures[iLevel].mVkImageView,
		mpBlueLightingBlurTextures[iLevel].mVkImageView,
	};
	VkFramebufferCreateInfo vkBlurFramebufferInfo
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderPass = mpLightingBlurVkRenderPasses[iLevel],
		.attachmentCount = 3,
		.pAttachments = pBlurImageViews,
		.width = static_cast<uint32_t>(iBlurTextureX),
		.height = static_cast<uint32_t>(iBlurTextureY),
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkBlurFramebufferInfo, nullptr, &mpLightingBlurVkFramebuffers[iLevel]));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mpLightingBlurVkFramebuffers[iLevel], "LightingBlurMRT");
}

} // namespace engine

#endif // defined(BT_CLIENT)
