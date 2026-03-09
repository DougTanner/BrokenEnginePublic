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

void RenderTargetTextures::Create()
{
	CreateLightingTextures();
	CreateShadowTextures();
	CreateSmokeTextures();
	CreateWindTextures();
	CreateObjectShadowsTextures();
	CreateTerrainTextures();
}

void RenderTargetTextures::DestroyLightingTextures()
{
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

	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	int64_t iLightingBlurTextureX = iLightingTextureX;
	int64_t iLightingBlurTextureY = iLightingTextureY;
	float fDownscale = gLightingBlurDownscale.Get();
	miLightingBlurCount = 0;
	for (int64_t i = 0; i < shaders::kiMaxLightingBlurCount; ++i)
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
			.eTextureLayout = kShaderReadOnly,
		};

		mpRedLightingBlurTextures[i].Create(lightingBlurTextureInfo);
		lightingBlurTextureInfo.name = "GreenLightingBlur";
		mpGreenLightingBlurTextures[i].Create(lightingBlurTextureInfo);
		lightingBlurTextureInfo.name = "BlueLightingBlur";
		mpBlueLightingBlurTextures[i].Create(lightingBlurTextureInfo);
	}
	Log("kiMaxLightingBlurCount: {} -> miLightingBlurCount: {}", shaders::kiMaxLightingBlurCount, miLightingBlurCount);

	mppLightingFinalTextures[0] = &mpRedLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[1] = &mpGreenLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[2] = &mpBlueLightingBlurTextures[iCombineTextureIndex];
}

void RenderTargetTextures::CreateShadowTextures()
{
	auto [iShadowTextureX, iShadowTextureY] = TextureManager::DetailTextureSize(gWorldDetail.Get());
	Log("iShadowTexture: {} x {}", iShadowTextureX, iShadowTextureY);
	mShadowElevationTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "ShadowElevation",
		.flags = 0,
		.format = shaders::keElevationFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX + iShadowTextureX / 2), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {gpIslandTerrain->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});
	mShadowTexture.Create(
	{
		.textureFlags = {},
		.name = "Shadow",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kComputeReadWrite,
	});
	mShadowBlurTexture.Create(
	{
		.textureFlags = {},
		.name = "ShadowBlur",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
}

void RenderTargetTextures::CreateSmokeTextures()
{
	gbSmokeClear = true;

	int64_t iGradientSize = 128;
	mSmokeGradientTexture.Create(
	{
		.textureFlags = {},
		.name = "SmokeTrailGradient",
		.flags = 0,
		.format = VK_FORMAT_R16_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iGradientSize), static_cast<uint32_t>(iGradientSize), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[&](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		float fCenter = static_cast<float>(iGradientSize / 2);
		float fPower = gSmokeTrailPower.Get();
		float fAlpha = gSmokeTrailAlpha.Get();

		uint16_t* puiColor = static_cast<uint16_t*>(pData);
		for (int64_t j = 0; j < iGradientSize; ++j)
		{
			for (int64_t i = 0; i < iGradientSize; ++i)
			{
				float fX = -fCenter + 0.5f + static_cast<float>(i);
				float fY = fCenter - 0.5f - static_cast<float>(j);
				float fDistance = std::pow(std::sqrt(fX * fX + fY * fY) / fCenter, fPower);
				float fIntensity = 1.0f - std::pow(fDistance, fAlpha) / (std::pow(fDistance, fAlpha) + std::pow((1.0f - fDistance), fAlpha));

				uint16_t uiR = static_cast<uint16_t>(static_cast<float>(std::numeric_limits<uint16_t>::max()) * fIntensity);
				puiColor[j * iGradientSize + i] = uiR;
			}
		}
	});

	TextureInfo smokeTextureInfo
	{
		.textureFlags = {kRenderPass},
		.name = "SmokeOne",
		.flags = 0,
		.format = shaders::keSmokeFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(SmokeSimulationPixels()), static_cast<uint32_t>(SmokeSimulationPixels()), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	};
	mSmokeTextureOne.Create(smokeTextureInfo);
	smokeTextureInfo.name = "SmokeTwo";
	smokeTextureInfo.extent = VkExtent3D {static_cast<uint32_t>(1.25f * SmokeSimulationPixels()), static_cast<uint32_t>(1.25f * SmokeSimulationPixels()), 1};
	smokeTextureInfo.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	mSmokeTextureTwo.Create(smokeTextureInfo);
}

void RenderTargetTextures::CreateWindTextures()
{
	gbWindClear = true;

	TextureInfo windTextureInfo
	{
		.textureFlags = {kRenderPass},
		.name = "WindOne",
		.flags = 0,
		.format = shaders::keWindFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(SmokeSimulationPixels()), static_cast<uint32_t>(SmokeSimulationPixels()), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	};
	mWindTextureOne.Create(windTextureInfo);

	mWindTextureTwo.Create(TextureInfo
	{
		.textureFlags = {kRenderPass},
		.name = "WindTwo",
		.flags = 0,
		.format = shaders::keWindFormat,
		.extent = windTextureInfo.extent,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	});
}

void RenderTargetTextures::CreateObjectShadowsTextures()
{
	auto [iObjectShadowsRenderTextureX, iObjectShadowsRenderTextureY] = TextureManager::DetailTextureSize(gObjectShadowsRenderMultiplier.Get());
	mObjectShadowsTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "ObjectShadows",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iObjectShadowsRenderTextureX), static_cast<uint32_t>(iObjectShadowsRenderTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {1.0f, 0.0f, 0.0f, 0.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iObjectShadowsBlurTextureX, iObjectShadowsBlurTextureY] = TextureManager::DetailTextureSize(gObjectShadowsBlurMultiplier.Get());
	mObjectShadowsBlurTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "ObjectShadowsBlur",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iObjectShadowsBlurTextureX), static_cast<uint32_t>(iObjectShadowsBlurTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	});
}

void RenderTargetTextures::CreateTerrainTextures()
{
	if constexpr (kbEnableDebugPrintf)
	{
		mLogTexture.Create(
		{
			.textureFlags = {kRenderPass},
			.name = "Log",
			.flags = 0,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.extent = VkExtent3D {32, 32, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.renderPassVkClearColorValue = {0.0f, 0.0f, 0.0f, 1.0f},
			.eTextureLayout = kShaderReadOnly,
		});
	}

	auto [iTerrainElevationTextureX, iTerrainElevationTextureY] = TextureManager::DetailTextureSize(gTerrainElevationTextureMultiplier.Get());
	mTerrainElevationTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "Elevation",
		.flags = 0,
		.format = shaders::keElevationFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iTerrainElevationTextureX), static_cast<uint32_t>(iTerrainElevationTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {gpIslandTerrain->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainColorTextureX, iTerrainColorTextureY] = TextureManager::DetailTextureSize(gTerrainColorTextureMultiplier.Get());
	mTerrainColorTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "TerrainColor",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iTerrainColorTextureX), static_cast<uint32_t>(iTerrainColorTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {shaders::kf4MudColor.x, shaders::kf4MudColor.y, shaders::kf4MudColor.z, shaders::kf4MudColor.w},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainNormalTextureX, iTerrainNormalTextureY] = TextureManager::DetailTextureSize(gTerrainNormalTextureMultiplier.Get());
	mTerrainNormalTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "Normal",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iTerrainNormalTextureX), static_cast<uint32_t>(iTerrainNormalTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {0.0f, 0.0f, 0.0f, 0.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainAmbientOcclusionTextureX, iTerrainAmbientOcclusionTextureY] = TextureManager::DetailTextureSize(gTerrainAmbientOcclusionTextureMultiplier.Get());
	mTerrainAmbientOcclusionTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "Ambient Occlusion",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iTerrainAmbientOcclusionTextureX), static_cast<uint32_t>(iTerrainAmbientOcclusionTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {0.0f, 0.0f, 1.0f, 0.0f},
		.eTextureLayout = kShaderReadOnly,
	});
}

} // namespace engine

#endif // BT_CLIENT
