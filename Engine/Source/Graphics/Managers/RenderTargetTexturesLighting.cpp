#if defined(BT_CLIENT)

#include "RenderTargetTextures.h"

#include "TextureManager.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

void RenderTargetTextures::DestroyLightingTextures()
{
	miDebugTextureCount = 0;

	for (int64_t iPass = 0; iPass < shaders::kiMaxLightingSpreadPasses; ++iPass)
	{
		for (int64_t i = 0; i < 3; ++i)
		{
			mpSpreadTextures[iPass][i].Destroy();
		}
	}

	for (int64_t i = 0; i < 3; ++i)
	{
		mpAccumulateTextures[i].Destroy();
		mpCombineTextures[i].Destroy();
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

	auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingDepositTextureMultiplier.Get());
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

	// Create spread textures: first spread has its own size, remaining spreads share a single size
	float fFirstSpreadMult = gFirstSpreadTextureMultiplier.Get();
	float fSpreadMult = gSpreadTextureMultiplier.Get();
	int64_t iPassCount = static_cast<int64_t>(gLightingSpreadPassCount.Get());
	static constexpr std::string_view pColorNames[3] {"Red", "Green", "Blue"};
	auto [iFirstSpreadX, iFirstSpreadY] = TextureManager::DetailTextureSize(fFirstSpreadMult);
	auto [iSpreadX, iSpreadY] = TextureManager::DetailTextureSize(fSpreadMult);
	for (int64_t iPass = 0; iPass < shaders::kiMaxLightingSpreadPasses; ++iPass)
	{
		int64_t iPassWidth = iPass == 0 ? iFirstSpreadX : iSpreadX;
		int64_t iPassHeight = iPass == 0 ? iFirstSpreadY : iSpreadY;
		for (int64_t i = 0; i < 3; ++i)
		{
			mpSpreadTextures[iPass][i].Create(TextureInfo
			{
				.textureFlags = {},
				.name = std::format("Spread{}{}", pColorNames[i], iPass),
				.flags = 0,
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.extent = VkExtent3D {static_cast<uint32_t>(iPassWidth), static_cast<uint32_t>(iPassHeight), 1},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.eTextureLayout = kShaderReadOnly,
			});
		}
	}

	// Accumulate and combine use the first spread (highest spread) resolution

	// Create accumulate textures (final summed output)
	static constexpr std::string_view pAccumulateNames[3] {"AccumulateRed", "AccumulateGreen", "AccumulateBlue"};
	for (int64_t i = 0; i < 3; ++i)
	{
		mpAccumulateTextures[i].Create(TextureInfo
		{
			.textureFlags = {},
			.name = pAccumulateNames[i],
			.flags = 0,
			.format = VK_FORMAT_R16G16B16A16_SFLOAT,
			.extent = VkExtent3D {static_cast<uint32_t>(iSpreadX), static_cast<uint32_t>(iSpreadY), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		});
	}

	// Create combine textures (UNORM tone-mapped output)
	static constexpr std::string_view pCombineNames[3] {"CombineRed", "CombineGreen", "CombineBlue"};
	for (int64_t i = 0; i < 3; ++i)
	{
		mpCombineTextures[i].Create(TextureInfo
		{
			.textureFlags = {},
			.name = pCombineNames[i],
			.flags = 0,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.extent = VkExtent3D {static_cast<uint32_t>(iSpreadX), static_cast<uint32_t>(iSpreadY), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		});
	}

	// Final output points to combine textures (tone-mapped UNORM)
	for (int64_t i = 0; i < 3; ++i)
	{
		mppLightingFinalTextures[i] = &mpCombineTextures[i];
	}

	// Debug textures: combine red (stable index 0), deposit red, spread[0..N-1] red
	mppDebugTextures[0] = &mpCombineTextures[0];
	mpDebugTextureFormats[0] = shaders::kiDebugTextureFormatUnormLightingDirectional;
	mppDebugTextures[1] = &mpLightingTextures[0];
	mpDebugTextureFormats[1] = shaders::kiDebugTextureFormatFloat16LightingDirectional;
	for (int64_t iPass = 0; iPass < iPassCount; ++iPass)
	{
		mppDebugTextures[2 + iPass] = &mpSpreadTextures[iPass][0];
		mpDebugTextureFormats[2 + iPass] = shaders::kiDebugTextureFormatFloat16LightingDirectional;
	}
	miDebugTextureCount = 2 + iPassCount;
	for (int64_t i = miDebugTextureCount; i < shaders::kiMaxDebugTextures; ++i)
	{
		mppDebugTextures[i] = &mpLightingTextures[0];
		mpDebugTextureFormats[i] = shaders::kiDebugTextureFormatFloat16LightingDirectional;
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
