#if defined(BT_CLIENT)

#include "RenderTargetTextures.h"

#include "TextureManager.h"
#include "Ui/LightingWrappersBase.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

void RenderTargetTextures::DestroyLightingTextures()
{
	miDebugTextureCount = 0;

	for (int64_t i = 0; i < 3; ++i)
	{
		mpCombineTextures[i].Destroy();
		mpLightingHistoryTextures[i].Destroy();
	}
	mAmbientCombineTexture.Destroy();
	mAmbientHistoryTexture.Destroy();

	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		for (int64_t iColor = 0; iColor < 3; ++iColor)
		{
			mpSpreadTextures[iPass][iColor].Destroy();
			mpSpreadOnlyTextures[iPass][iColor].Destroy();
		}

		if (mpSpreadVkFramebuffers[iPass] != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mpSpreadVkFramebuffers[iPass], nullptr);
			mpSpreadVkFramebuffers[iPass] = VK_NULL_HANDLE;
		}
	}

	if (mSpreadVkRenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mSpreadVkRenderPass, nullptr);
		mSpreadVkRenderPass = VK_NULL_HANDLE;
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

	// This (re)create rebuilds the history textures (below) with undefined contents; re-arm the temporal first-frame
	// guard so PopulateLightingParameters blends pure-current and re-seeds history next frame (mirror of CreateShadowTextures).
	gbLightingTemporalReset = true;

	auto [iLightingTextureX, iLightingTextureY] = TextureManager::LightingDetailTextureSize(gLightingDepositTextureMultiplier.Get());
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
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback (shared by all 3 lighting textures)
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
	VkAttachmentDescription pVkAttachmentDescriptions[3] {};
	for (int64_t i = 0; i < 3; ++i)
	{
		pVkAttachmentDescriptions[i] = VkAttachmentDescription
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
		};
	}
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
	// The UNDEFINED transition and clear must wait for the previous frame's shader reads of these shared textures: the implicit
	// external dependency the spec supplies is TOP_OF_PIPE/0 and orders nothing, and frames overlap up to the swapchain image count.
	VkSubpassDependency pVkSubpassDependencies[2]
	{
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0, // Write-after-read needs execution ordering only
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = 0,
		},
		{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dependencyFlags = 0,
		},
	};
	VkRenderPassCreateInfo vkRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = static_cast<uint32_t>(std::size(pVkAttachmentDescriptions)),
		.pAttachments = pVkAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSubpassDescription,
		.dependencyCount = static_cast<uint32_t>(std::size(pVkSubpassDependencies)),
		.pDependencies = pVkSubpassDependencies,
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
		.attachmentCount = static_cast<uint32_t>(std::size(pVkImageViews)),
		.pAttachments = pVkImageViews,
		.width = static_cast<uint32_t>(iLightingTextureX),
		.height = static_cast<uint32_t>(iLightingTextureY),
		.layers = 1,
	};
	CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkFramebufferCreateInfo, nullptr, &mLightingVkFramebuffer));
	VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mLightingVkFramebuffer, "LightingMRT");

	// Create spread textures (MRT color attachments, one set per pass, interpolated size)
	float fSpreadMultStart = gSpreadTextureMultiplierStart.Get();
	float fSpreadMultEnd = gSpreadTextureMultiplierEnd.Get();
	int64_t iPassCount = static_cast<int64_t>(gSpreadPassCount.Get());
	static constexpr std::string_view pColorNames[3] {"Red", "Green", "Blue"};
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		float fT = (iPassCount > 1) ? static_cast<float>(iPass) / static_cast<float>(iPassCount - 1) : 0.0f;
		float fMult = fSpreadMultStart + fT * (fSpreadMultEnd - fSpreadMultStart);
		auto [iPassX, iPassY] = TextureManager::LightingDetailTextureSize(fMult);
		for (int64_t iColor = 0; iColor < 3; ++iColor)
		{
			std::string strSpreadName = std::format("Spread{}_{}", pColorNames[iColor], iPass);
			mpSpreadTextures[iPass][iColor].Create(TextureInfo
			{
				.textureFlags = {},
				.name = strSpreadName,
				.flags = 0,
				.format = shaders::keLightingFormat,
				.extent = VkExtent3D {static_cast<uint32_t>(iPassX), static_cast<uint32_t>(iPassY), 1},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.eTextureLayout = kShaderReadOnly,
			});
			std::string strSpreadOnlyName = std::format("SpreadOnly{}_{}", pColorNames[iColor], iPass);
			mpSpreadOnlyTextures[iPass][iColor].Create(TextureInfo
			{
				.textureFlags = {},
				.name = strSpreadOnlyName,
				.flags = 0,
				.format = shaders::keLightingFormat,
				.extent = VkExtent3D {static_cast<uint32_t>(iPassX), static_cast<uint32_t>(iPassY), 1},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.eTextureLayout = kShaderReadOnly,
			});
		}
	}

	// Create spread MRT render pass (6 color attachments: 3 accumulated + 3 spread-only)
	// Locations 0–2: accumulated (fed to next spread pass). Locations 3–5: spread-only (read by combine).
	VkAttachmentDescription pSpreadAttachmentDescriptions[6] {};
	VkAttachmentReference pSpreadAttachmentReferences[6] {};
	for (int64_t i = 0; i < 6; ++i)
	{
		pSpreadAttachmentDescriptions[i] = VkAttachmentDescription
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
		};
		pSpreadAttachmentReferences[i] = {.attachment = static_cast<uint32_t>(i), .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	}
	VkSubpassDescription vkSpreadSubpassDescription
	{
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 6,
		.pColorAttachments = pSpreadAttachmentReferences,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};
	// The UNDEFINED transition and clear must wait for the previous frame's shader reads of these shared textures: the implicit
	// external dependency the spec supplies is TOP_OF_PIPE/0 and orders nothing, and frames overlap up to the swapchain image count.
	VkSubpassDependency pVkSpreadSubpassDependencies[2]
	{
		{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0, // Write-after-read needs execution ordering only
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = 0,
		},
		{
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dependencyFlags = 0,
		},
	};
	VkRenderPassCreateInfo vkSpreadRenderPassCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = static_cast<uint32_t>(std::size(pSpreadAttachmentDescriptions)),
		.pAttachments = pSpreadAttachmentDescriptions,
		.subpassCount = 1,
		.pSubpasses = &vkSpreadSubpassDescription,
		.dependencyCount = static_cast<uint32_t>(std::size(pVkSpreadSubpassDependencies)),
		.pDependencies = pVkSpreadSubpassDependencies,
	};
	CHECK_VK(vkCreateRenderPass(gpDeviceManager->mVkDevice, &vkSpreadRenderPassCreateInfo, nullptr, &mSpreadVkRenderPass));
	VkName(VK_OBJECT_TYPE_RENDER_PASS, mSpreadVkRenderPass, "SpreadMRT");

	// Create spread framebuffers (one per spread pass, each binding 3 accumulated + 3 spread-only textures)
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		VkImageView pSpreadImageViews[6]
		{
			mpSpreadTextures[iPass][0].mVkImageView,
			mpSpreadTextures[iPass][1].mVkImageView,
			mpSpreadTextures[iPass][2].mVkImageView,
			mpSpreadOnlyTextures[iPass][0].mVkImageView,
			mpSpreadOnlyTextures[iPass][1].mVkImageView,
			mpSpreadOnlyTextures[iPass][2].mVkImageView,
		};
		VkFramebufferCreateInfo vkSpreadFramebufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.renderPass = mSpreadVkRenderPass,
			.attachmentCount = static_cast<uint32_t>(std::size(pSpreadImageViews)),
			.pAttachments = pSpreadImageViews,
			.width = mpSpreadTextures[iPass][0].mInfo.extent.width,
			.height = mpSpreadTextures[iPass][0].mInfo.extent.height,
			.layers = 1,
		};
		CHECK_VK(vkCreateFramebuffer(gpDeviceManager->mVkDevice, &vkSpreadFramebufferCreateInfo, nullptr, &mpSpreadVkFramebuffers[iPass]));
		VkName(VK_OBJECT_TYPE_FRAMEBUFFER, mpSpreadVkFramebuffers[iPass], std::format("SpreadMRT_{}", iPass));
	}

	// Create combine textures (UNORM tone-mapped output, sized to max of start/end). LightingTemporal.comp blends the
	// previous-frame history into these in place; LightingHistoryCopy.comp publishes the result to history.
	auto [iCombineX, iCombineY] = TextureManager::LightingDetailTextureSize(std::max(fSpreadMultStart, fSpreadMultEnd));
	static constexpr std::string_view pCombineNames[3] {"CombineRed", "CombineGreen", "CombineBlue"};
	for (int64_t i = 0; i < 3; ++i)
	{
		mpCombineTextures[i].Create(TextureInfo
		{
			.textureFlags = {},
			.name = pCombineNames[i],
			.flags = 0,
			.format = shaders::keCombineFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(iCombineX), static_cast<uint32_t>(iCombineY), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		});
	}
	mAmbientCombineTexture.Create(TextureInfo
	{
		.textureFlags = {},
		.name = "CombineAmbient",
		.flags = 0,
		.format = shaders::keCombineFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iCombineX), static_cast<uint32_t>(iCombineY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});

	// History textures: previous-frame combine outputs reprojected + EMA-blended by LightingTemporal.comp.
	// Clones of the combine textures (same bumped extent, RGBA8 UNORM); SAMPLED (read as history), STORAGE
	// (descriptor-set compatibility with combine bindings) so LightingHistoryCopy.comp can refresh them in-place.
	static constexpr std::string_view pHistoryNames[3] {"LightingHistoryRed", "LightingHistoryGreen", "LightingHistoryBlue"};
	for (int64_t i = 0; i < 3; ++i)
	{
		mpLightingHistoryTextures[i].Create(TextureInfo
		{
			.textureFlags = {},
			.name = pHistoryNames[i],
			.flags = 0,
			.format = shaders::keCombineFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(iCombineX), static_cast<uint32_t>(iCombineY), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		});
	}
	mAmbientHistoryTexture.Create(TextureInfo
	{
		.textureFlags = {},
		.name = "LightingHistoryAmbient",
		.flags = 0,
		.format = shaders::keCombineFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iCombineX), static_cast<uint32_t>(iCombineY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});

	// Final output points to combine textures (tone-mapped UNORM)
	for (int64_t i = 0; i < 3; ++i)
	{
		mppLightingFinalTextures[i] = &mpCombineTextures[i];
		mppLightingDepositTextures[i] = &mpLightingTextures[i];
	}

	RegisterDebugTextures(iPassCount);
}

void RenderTargetTextures::RegisterDebugTextures(int64_t iPassCount)
{
	// Debug textures: terrain G-buffer (elevation only — color/normal/AO composite RTTs were deleted
	// when Terrain.frag switched to direct bindless per-island sampling), then deposit RGB (visible
	// area), deposit combined direction, spread pass 0..N combined direction, combine red.
	static constexpr int64_t kiTerrainDebugSlotCount = 1;
	mppDebugTextures[0] = &mTerrainElevationTexture;
	mpDebugTextureFormats[0] = shaders::kiDebugTextureFormatTerrainElevation;

	mppDebugTextures[kiTerrainDebugSlotCount + 0] = &mpLightingTextures[0];
	mpDebugTextureFormats[kiTerrainDebugSlotCount + 0] = shaders::kiDebugTextureFormatFloat16LinearVisibleArea;
	mppDebugTextures[kiTerrainDebugSlotCount + 1] = &mpLightingTextures[0];
	mpDebugTextureFormats[kiTerrainDebugSlotCount + 1] = shaders::kiDebugTextureFormatFloat16DepositDirectionCombined;
	for (int64_t i = 0; i < iPassCount; ++i)
	{
		mppDebugTextures[kiTerrainDebugSlotCount + 2 + i] = &mpSpreadTextures[i][0];
		mppDebugTexturesB[kiTerrainDebugSlotCount + 2 + i] = &mpSpreadTextures[i][1];
		mppDebugTexturesC[kiTerrainDebugSlotCount + 2 + i] = &mpSpreadTextures[i][2];
		mpDebugTextureFormats[kiTerrainDebugSlotCount + 2 + i] = shaders::kiDebugTextureFormatFloat16SpreadDirectionCombined;
	}
	mppDebugTextures[kiTerrainDebugSlotCount + 2 + iPassCount] = &mpCombineTextures[0];
	mpDebugTextureFormats[kiTerrainDebugSlotCount + 2 + iPassCount] = shaders::kiDebugTextureFormatUnormLightingDirectional;
	miDebugTextureCount = kiTerrainDebugSlotCount + 3 + iPassCount;

	// Fill unused B/C slots with primary texture so descriptor writes remain valid
	for (int64_t i = 0; i < shaders::kiMaxDebugTextures; ++i)
	{
		if (mppDebugTexturesB[i] == nullptr)
		{
			mppDebugTexturesB[i] = mppDebugTextures[i] != nullptr ? mppDebugTextures[i] : &mpLightingTextures[0];
		}
		if (mppDebugTexturesC[i] == nullptr)
		{
			mppDebugTexturesC[i] = mppDebugTextures[i] != nullptr ? mppDebugTextures[i] : &mpLightingTextures[0];
		}
		if (mppDebugTextures[i] == nullptr)
		{
			mppDebugTextures[i] = &mpLightingTextures[0];
		}
	}

	if (gDebugTextureIndex.Get() >= static_cast<float>(miDebugTextureCount))
	{
		gDebugTextureIndex.Set(static_cast<float>(miDebugTextureCount - 1));
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
