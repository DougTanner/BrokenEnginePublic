#if defined(BT_CLIENT)

#include "RenderTargetTextures.h"

#include "TextureManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

void RenderTargetTextures::Create()
{
	CreateLightingTextures();
	CreateShadowTextures();
	CreateSmokeTextures();
	CreateWindTextures();
	CreateObjectShadowsTextures();
	CreateTerrainTextures();
	CreateWaterDisplacementTextures();
}

void RenderTargetTextures::CreateWaterDisplacementTextures()
{
	// Pre-computed Gerstner displacement + normal sampled by Water.vert (both water pipelines)
	// instead of re-evaluating the wave sum twice per frame. Texel grid is in 1:1 alignment with
	// the LOD0 water-mesh vertex grid built in BufferManager::CreateWaterMesh — same WaterDetailTextureSize call.
	auto [iWaterX, iWaterY] = gpTextureManager->WaterDetailTextureSize(gWaterShapeDetail.Get());
	mWaterDisplacementTexture.Create(
	{
		.textureFlags = {},
		.name = "WaterDisplacement",
		.flags = 0,
		.format = shaders::keWaterDisplacementFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iWaterX), static_cast<uint32_t>(iWaterY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
	mWaterDisplacementNormalTexture.Create(
	{
		.textureFlags = {},
		.name = "WaterDisplacementNormal",
		.flags = 0,
		.format = shaders::keWaterDisplacementFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iWaterX), static_cast<uint32_t>(iWaterY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
}

void RenderTargetTextures::CreateShadowTextures()
{
	// Fixed-world-size shadow texels: pre-size the texture to cover the visible area at the reference eye
	// height so the texel scale is constant across zoom (no pop/shimmer); closer zoom uses a centered
	// sub-window, farther crops. Clamp AFTER the multiply (DetailTextureSize clamps pre-multiply); cap the
	// width at (limit/3)*2 so the 1.5x-wide elevation texture still fits maxImageDimension2D. Force the
	// width even (below) so the 1.5x elevation width stays integer; dispatch ceil-divides so no block multiple.

	// Re-arm the temporal first-frame guard: mShadowHistoryTexture below is (re)created with undefined contents,
	// so PopulateShadowParameters must blend pure-current for one frame before reusing history. Mirrors gbSmokeClear
	// (and CreateWindTextures' creation-time hard clear).
	gbShadowTemporalReset = true;

	auto [iBaseX, iBaseY] = TextureManager::DetailTextureSize(gShadowRenderMultiplier.Get());
	int64_t iLimit = static_cast<int64_t>(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxImageDimension2D);
	int64_t iShadowTextureX = std::min(static_cast<int64_t>(std::lround(static_cast<float>(iBaseX) * game::Camera::kfShadowHeadroomMultiplier)), (iLimit / 3) * 2);
	iShadowTextureX &= ~1ll;
	int64_t iShadowTextureY = std::min(static_cast<int64_t>(std::lround(static_cast<float>(iBaseY) * game::Camera::kfShadowHeadroomMultiplier)), iLimit);
	LOG(kGraphics, kDebug, "iShadowTexture: {} x {}", iShadowTextureX, iShadowTextureY);
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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {gpIslandTerrain->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
		.renderPassDstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	});
	mShadowTexture.Create(
	{
		.textureFlags = {},
		.name = "Shadow",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kComputeReadWrite,
	});
	mShadowBlurTexture.Create(
	{
		.textureFlags = {},
		.name = "ShadowBlur",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
	mShadowBlurIntermediateTexture.Create(
	{
		.textureFlags = {},
		.name = "ShadowBlurIntermediate",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
	mShadowHistoryTexture.Create(
	{
		.textureFlags = {},
		.name = "ShadowHistory",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[&](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		float fCenter = static_cast<float>(iGradientSize) * 0.5f;
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
		.extent = VkExtent3D {static_cast<uint32_t>(SmokeSimulationPixels()), static_cast<uint32_t>(SmokeSimulationPixelsY()), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback (shared by SmokeOne/SmokeTwo)
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	};
	mSmokeTextureOne.Create(smokeTextureInfo);
	smokeTextureInfo.name = "SmokeTwo";
	smokeTextureInfo.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	mSmokeTextureTwo.Create(smokeTextureInfo);

	// Smoke spread only rewrites occupancy-active tiles, so both ping-pong textures must start at zero.
	OneShotCommandBuffer oneShotCommandBuffer;
	VkClearColorValue vkSmokeClearColor {{0.0f, 0.0f, 0.0f, 0.0f}};
	VkImageSubresourceRange vkSmokeSubresource {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
	for (Texture* pSmokeTexture : {&mSmokeTextureOne, &mSmokeTextureTwo})
	{
		pSmokeTexture->TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kShaderReadOnly, kTransferDestination);
		vkCmdClearColorImage(oneShotCommandBuffer.mVkCommandBuffer, pSmokeTexture->mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkSmokeClearColor, 1, &vkSmokeSubresource);
		pSmokeTexture->TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	}
	oneShotCommandBuffer.Execute();
}

void RenderTargetTextures::CreateWindTextures()
{
	TextureInfo windTextureInfo
	{
		.textureFlags = {kRenderPass},
		.name = "WindOne",
		.flags = 0,
		.format = shaders::keWindFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(SmokeSimulationPixels()), static_cast<uint32_t>(SmokeSimulationPixelsY()), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kShaderReadOnly,
	});

	// Wind ping-pong textures are (re)created with undefined contents. On a device-lost / settings recreate the wind
	// spread only touches active tiles (occupancy-driven), so it never decays garbage in inactive tiles, and smoke
	// samples that garbage for several frames. Hard-clear both to zero once here (the device is idle on the
	// recreate/boot path). Like smoke, the recreate edge is closed with a one-time creation clear rather than a
	// per-frame full-texture clear.
	OneShotCommandBuffer oneShotCommandBuffer;
	VkClearColorValue vkWindClearColor {{0.0f, 0.0f, 0.0f, 0.0f}};
	VkImageSubresourceRange vkWindSubresource {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
	for (Texture* pWindTexture : {&mWindTextureOne, &mWindTextureTwo})
	{
		pWindTexture->TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kShaderReadOnly, kTransferDestination);
		vkCmdClearColorImage(oneShotCommandBuffer.mVkCommandBuffer, pWindTexture->mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkWindClearColor, 1, &vkWindSubresource);
		pWindTexture->TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	}
	oneShotCommandBuffer.Execute();
}

void RenderTargetTextures::CreateObjectShadowsTextures()
{
	auto [iObjectShadowsRenderTextureX, iObjectShadowsRenderTextureY] = TextureManager::DetailTextureSize(gObjectShadowsRenderMultiplier.Get());
	mObjectShadowsTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "ObjectShadows",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iObjectShadowsRenderTextureX), static_cast<uint32_t>(iObjectShadowsRenderTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {1.0f, 0.0f, 0.0f, 0.0f},
		.eTextureLayout = kShaderReadOnly,
		.renderPassDstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	});

	auto [iObjectShadowsBlurTextureX, iObjectShadowsBlurTextureY] = TextureManager::DetailTextureSize(gObjectShadowsBlurMultiplier.Get());
	mObjectShadowsBlurTexture.Create(
	{
		.textureFlags = {},
		.name = "ObjectShadowsBlur",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iObjectShadowsBlurTextureX), static_cast<uint32_t>(iObjectShadowsBlurTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
	mObjectShadowsBlurIntermediateTexture.Create(
	{
		.textureFlags = {},
		.name = "ObjectShadowsBlurIntermediate",
		.flags = 0,
		.format = shaders::keShadowFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iObjectShadowsBlurTextureX), static_cast<uint32_t>(iObjectShadowsBlurTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});
}

void RenderTargetTextures::CreateTerrainTextures()
{
	if constexpr (kbDebugPrintf)
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
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // TRANSFER_SRC: agent dump_render_target readback
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {gpIslandTerrain->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});
}

} // namespace engine

#endif // defined(BT_CLIENT)
