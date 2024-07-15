#include "TextureManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Graphics/OneShotCommandBuffer.h"
#include "Profile/ProfileManager.h"
#include "Frame/Pools/Smoke.h"

#include "Frame/Frame.h"

using namespace DirectX;

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

std::tuple<int64_t, int64_t> CombineTextureInfo()
{
	int64_t iCombineTextureIndex = static_cast<int64_t>(gLightingCombineIndex.Get());
	int64_t iBlurTextureCount = gpTextureManager->miLightingBlurCount - iCombineTextureIndex - 1;
	return std::make_tuple(iCombineTextureIndex, iBlurTextureCount);
}

std::tuple<int64_t, int64_t> TextureManager::DetailTextureSize(float fMultiplier)
{
	auto [iWorldDetailX, iWorldDetailY] = FullDetail();

	int64_t iX = static_cast<int64_t>(fMultiplier * static_cast<float>(iWorldDetailX));
	int64_t iY = static_cast<int64_t>(fMultiplier * static_cast<float>(iWorldDetailY));

	iX = std::max(iX, 128i64);
	iY = std::max(iY, 64i64);

	iX = std::min(iX, static_cast<int64_t>(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxImageDimension2D));
	iY = std::min(iY, static_cast<int64_t>(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxImageDimension2D));

	return std::make_tuple(iX, iY);
}

float TextureManager::DetailTextureAspectRatio()
{
	auto [iWorldDetailX, iWorldDetailY] = FullDetail();
	return static_cast<float>(iWorldDetailX) / static_cast<float>(iWorldDetailY);
}

TextureManager::TextureManager()
{
	gpTextureManager = this;

	SCOPED_BOOT_TIMER(kBootTimerTextureManager);

	CreateSamplers();

	CreateLightingTextures();
	CreateShadowTextures();
	CreateSmokeTextures();
	CreateObjectShadowsTextures();

#if defined(ENABLE_DEBUG_PRINTF_EXT)
	mLogTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "Log",
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
#endif

	int64_t iMissileTextureSize = 32;
	mMissileTexture.Create(
	{
		.textureFlags = {},
		.pcName = "Missile",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iMissileTextureSize), static_cast<uint32_t>(iMissileTextureSize), 1},
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
		memset(pData, 0x55, iSize);
	});

	auto [iTerrainElevationTextureX, iTerrainElevationTextureY] = DetailTextureSize(gTerrainElevationTextureMultiplier.Get());
	mTerrainElevationTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "Elevation",
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
		.renderPassVkClearColorValue = {gpIslands->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainColorTextureX, iTerrainColorTextureY] = DetailTextureSize(gTerrainColorTextureMultiplier.Get());
	mTerrainColorTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "TerrainColor",
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

	auto [iTerrainNormalTextureX, iTerrainNormalTextureY] = DetailTextureSize(gTerrainNormalTextureMultiplier.Get());
	mTerrainNormalTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "Normal",
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

	auto [iTerrainAmbientOcclusionTextureX, iTerrainAmbientOcclusionTextureY] = DetailTextureSize(gTerrainAmbientOcclusionTextureMultiplier.Get());
	mTerrainAmbientOcclusionTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "Ambient Occlusion",
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

	auto& rChunkMap = gpFileManager->GetTexturesChunkMap();

	BOOT_TIMER_START(kBootTimerTextureUpload);
	for (auto& [rCrc, rChunk] : rChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kTexture))
		{
			continue;
		}

		bool bCubemap = rChunk.pHeader->flags & common::ChunkFlags::kCubemap;
		auto [it, bInserted] = mTextureMap.try_emplace(rChunk.pHeader->crc, TextureInfo
		{
			.textureFlags = {},
			.pcName = rChunk.pHeader->pcPath,
			.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
			.format = rChunk.pHeader->textureHeader.vkFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(rChunk.pHeader->textureHeader.iTextureWidth), static_cast<uint32_t>(rChunk.pHeader->textureHeader.iTextureHeight), 1},
			.mipLevels = static_cast<uint32_t>(rChunk.pHeader->textureHeader.iMipLevels),
			.arrayLayers = bCubemap ? 6u : 1u,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.viewType = bCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		},
		[&](void* pData, int64_t iPosition, int64_t iSize)
		{
			memcpy(pData, &rChunk.pData[iPosition], iSize);
		});
		ASSERT(bInserted);
	}

	// You need to manually change kiTextureCount/kiUiTextureCount in ShaderLayoutsBase.h to match the same values in Data.h
	static_assert(data::kiTextureCount == shaders::kiTextureCount);
	static_assert(data::kiUiTextureCount == shaders::kiUiTextureCount);
	// DT: TODO In tools, can export textures before shaders, generate a texture header, then compile shaders after?

	int64_t iIndex = 0;
	for (const auto& rCrc : data::kpTextureCrcs)
	{
		mImageInfos.emplace_back(nullptr, mTextureMap.at(rCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		mImageInfosMap.try_emplace(rCrc, iIndex++);
	}
	ASSERT(mImageInfos.size() == shaders::kiTextureCount);
	iIndex = 0;
	for (const auto& rCrc : data::kpUiTextureCrcs)
	{
		mUiImageInfos.emplace_back(nullptr, mTextureMap.at(rCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		mUiImageInfosMap.try_emplace(rCrc, iIndex++);
	}
	ASSERT(mUiImageInfos.size() == shaders::kiUiTextureCount);

	for (int64_t i = 0; i < shaders::kiParticlesCookieCount; ++i)
	{
		mpSquareParticleTextures[i] = &mMissileTexture;
		mpLongParticleTextures[i] = &mMissileTexture;
	}
	for (int64_t i = 0; i < kSquareParticleCrcs.miCount; ++i)
	{
		mpSquareParticleTextures[i] = &mTextureMap.at(kSquareParticleCrcs[i]);
	}
	for (int64_t i = 0; i < kLongParticleCrcs.miCount; ++i)
	{
		mpLongParticleTextures[i] = &mTextureMap.at(kLongParticleCrcs[i]);
	}

	// Island textures
	mElevationTextures.resize(game::Frame::kiIslandCount);
	mColorTextures.resize(game::Frame::kiIslandCount);
	mNormalsTextures.resize(game::Frame::kiIslandCount);
	mAmbientOcclusionTextures.resize(game::Frame::kiIslandCount);

	iIndex = 0;
	auto& rDataChunkMap = gpFileManager->GetDataChunkMap();
	for (auto& [rCrc, rChunk] : rDataChunkMap)
	{
		if (!(rChunk.pHeader->flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		mElevationTextures[iIndex] = &gpTextureManager->mTextureMap.at(rChunk.pHeader->islandHeader.elevationCrc);
		mColorTextures[iIndex] = &gpTextureManager->mTextureMap.at(rChunk.pHeader->islandHeader.colorsCrc);
		mNormalsTextures[iIndex] = &gpTextureManager->mTextureMap.at(rChunk.pHeader->islandHeader.normalsCrc);
		mAmbientOcclusionTextures[iIndex] = &gpTextureManager->mTextureMap.at(rChunk.pHeader->islandHeader.ambientOcclusionCrc);

		++iIndex;
	}
	while (iIndex < game::Frame::kiIslandCount)
	{
		mElevationTextures[iIndex] = mElevationTextures[iIndex - 1];
		mColorTextures[iIndex] = mColorTextures[iIndex - 1];
		mNormalsTextures[iIndex] = mNormalsTextures[iIndex - 1];
		mAmbientOcclusionTextures[iIndex] = mAmbientOcclusionTextures[iIndex - 1];

		++iIndex;
	}

	BOOT_TIMER_STOP(kBootTimerTextureUpload);

	BOOT_TIMER_START(kGltfTexturesGeneration);
	GenerateGltfCubemap(true);
	GenerateGltfCubemap(false);
	GenerateGltfLutBrdf();
	BOOT_TIMER_STOP(kGltfTexturesGeneration);
}

TextureManager::~TextureManager()
{
	DestroySamplers();

	gpTextureManager = nullptr;
}

void TextureManager::DestroySamplers()
{
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerSmoke, nullptr);
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerBorder, nullptr);
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerClamp, nullptr);
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerRepeat, nullptr);
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerMirroredRepeat, nullptr);
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerNearestBorder, nullptr);
}

void TextureManager::CreateSamplers()
{
	if (gMaxAnisotropy.Get() > gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerAnisotropy)
	{
		gMaxAnisotropy.Reset(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerAnisotropy);
	}

	if (-gMipLodBias.Get() > gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias)
	{
		gMipLodBias.Reset(-gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias);
	}
	else if (gMipLodBias.Get() < -gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias)
	{
		gMipLodBias.Reset(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias);
	}

	VkSamplerCreateInfo smokeVkSamplerCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.mipLodBias = 0.0f,
		.anisotropyEnable = VK_FALSE,
		.maxAnisotropy = 0.0f,
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 14.0f,
		.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &smokeVkSamplerCreateInfo, nullptr, &mVkSamplerSmoke));

	VkSamplerCreateInfo vkSamplerCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias = -gMipLodBias.Get(),
		.anisotropyEnable = gAnisotropy.Get<bool>() ? VK_TRUE : VK_FALSE,
		.maxAnisotropy = gMaxAnisotropy.Get(),
		.compareEnable = VK_FALSE,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.minLod = 0.0f,
		.maxLod = 14.0f,
		.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerClamp));
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerBorder));
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerRepeat));
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerMirroredRepeat));
	vkSamplerCreateInfo.magFilter = VK_FILTER_NEAREST,
	vkSamplerCreateInfo.minFilter = VK_FILTER_NEAREST,
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.anisotropyEnable = VK_FALSE;
	vkSamplerCreateInfo.maxAnisotropy = 0.0f;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerNearestBorder));
}

void TextureManager::CreateLightingTextures()
{
	auto [iLightingTextureX, iLightingTextureY] = DetailTextureSize(gLightingTextureMultiplier.Get());
	TextureInfo lightingTextureInfo
	{
		.textureFlags = {kRenderPass},
		.pcName = "RedLighting",
		.flags = 0,
		.format = shaders::keLightingFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iLightingTextureX), static_cast<uint32_t>(iLightingTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.renderPassVkClearColorValue = {0.0f, 0.0f, 0.0f, 0.0f},
		.eTextureLayout = kShaderReadOnly,
	};
	mpLightingTextures[0].Create(lightingTextureInfo);
	lightingTextureInfo.pcName = "GreenLighting";
	mpLightingTextures[1].Create(lightingTextureInfo);
	lightingTextureInfo.pcName = "BlueLighting";
	mpLightingTextures[2].Create(lightingTextureInfo);

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
			.pcName = "RedLightingBlur",
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
		lightingBlurTextureInfo.pcName = "GreenLightingBlur";
		mpGreenLightingBlurTextures[i].Create(lightingBlurTextureInfo);
		lightingBlurTextureInfo.pcName = "BlueLightingBlur";
		mpBlueLightingBlurTextures[i].Create(lightingBlurTextureInfo);
	}
	LOG("kiMaxLightingBlurCount: {} -> miLightingBlurCount: {}", shaders::kiMaxLightingBlurCount, miLightingBlurCount);

	mppLightingFinalTextures[0] = &mpRedLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[1] = &mpGreenLightingBlurTextures[iCombineTextureIndex];
	mppLightingFinalTextures[2] = &mpBlueLightingBlurTextures[iCombineTextureIndex];
}

void TextureManager::CreateShadowTextures()
{
	auto [iShadowTextureX, iShadowTextureY] = DetailTextureSize(gWorldDetail.Get());
	LOG("iShadowTexture: {} x {}", iShadowTextureX, iShadowTextureY);
	mShadowElevationTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "ShadowElevation",
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
		.renderPassVkClearColorValue = {gpIslands->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});
	mShadowTexture.Create(
	{
		.textureFlags = {},
		.pcName = "Shadow",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {static_cast<uint32_t>(iShadowTextureX), static_cast<uint32_t>(iShadowTextureY), 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kComputeWrite,
	});
	mShadowBlurTexture.Create(
	{
		.textureFlags = {},
		.pcName = "ShadowBlur",
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

void TextureManager::CreateSmokeTextures()
{
	gbSmokeClear = true;

	int64_t iGradientSize = 128;
	mSmokeGradientTexture.Create(
	{
		.textureFlags = {},
		.pcName = "SmokeTrailGradient",
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

		auto puiColor = reinterpret_cast<uint16_t*>(pData);
		for (int64_t j = 0; j < iGradientSize; ++j)
		{
			for (int64_t i = 0; i < iGradientSize; ++i)
			{
				float fX = -fCenter + 0.5f + static_cast<float>(i);
				float fY = fCenter - 0.5f - static_cast<float>(j);
				float fDistance = std::pow(std::sqrt(fX * fX + fY * fY) / fCenter, fPower);
				float fIntensity = 1.0f - std::pow(fDistance, fAlpha) / (std::pow(fDistance, fAlpha) + std::pow((1.0f -fDistance), fAlpha));

				uint16_t uiR = static_cast<uint16_t>(static_cast<float>(std::numeric_limits<uint16_t>::max()) * fIntensity);
				puiColor[j * iGradientSize + i] = uiR;
			}
		}
	});

	TextureInfo smokeTextureInfo
	{
		.textureFlags = {kRenderPass},
		.pcName = "SmokeOne",
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
	smokeTextureInfo.pcName = "SmokeTwo";
	smokeTextureInfo.extent = VkExtent3D {static_cast<uint32_t>(0.85f * SmokeSimulationPixels()), static_cast<uint32_t>(0.85f * SmokeSimulationPixels()), 1};
	smokeTextureInfo.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	mSmokeTextureTwo.Create(smokeTextureInfo);
}

void TextureManager::CreateObjectShadowsTextures()
{
	auto [iObjectShadowsRenderTextureX, iObjectShadowsRenderTextureY] = DetailTextureSize(gObjectShadowsRenderMultiplier.Get());
	mObjectShadowsTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "ObjectShadows",
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

	auto [iObjectShadowsBlurTextureX, iObjectShadowsBlurTextureY] = DetailTextureSize(gObjectShadowsBlurMultiplier.Get());
	mObjectShadowsBlurTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "ObjectShadowsBlur",
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

VkSampler TextureManager::GetSampler(DescriptorFlags_t flags)
{
	if (flags & DescriptorFlags::kSamplerClamp)
	{
		return mVkSamplerClamp;
	}
	else if (flags & DescriptorFlags::kSamplerBorder)
	{
		return mVkSamplerBorder;
	}
	else if (flags & DescriptorFlags::kSamplerRepeat)
	{
		return mVkSamplerRepeat;
	}
	else if (flags & DescriptorFlags::kSamplerMirroredRepeat)
	{
		return mVkSamplerMirroredRepeat;
	}
	else if (flags & DescriptorFlags::kSamplerSmoke)
	{
		return mVkSamplerSmoke;
	}
	else if (flags & DescriptorFlags::kSamplerNearestBorder)
	{
		return mVkSamplerNearestBorder;
	}
	else
	{
		return mVkSamplerClamp;
	}
}

bool FormatSupportsColorAttachment(VkFormat vkFormat)
{
	VkFormatProperties vkFormatProperties {};
	vkGetPhysicalDeviceFormatProperties(gpInstanceManager->mVkPhysicalDevice, vkFormat, &vkFormatProperties);
	return (vkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

void TextureManager::GenerateGltfCubemap(bool bIrradiance)
{
	LOG("FormatSupportsColorAttachment? VK_FORMAT_R32G32B32A32_SFLOAT {} VK_FORMAT_R16G16B16A16_SFLOAT {}", FormatSupportsColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT), FormatSupportsColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT));

	// VK_FORMAT_R32G32B32A32_SFLOAT not supported on some GPUs?
	// VkFormat vkFormat = bIrradiance ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	int64_t iSize = bIrradiance ? 64 : 512;
	int64_t iMipCount = static_cast<int64_t>(std::floor(std::log2(iSize))) + 1;
	miGltfCubeMipCount = iMipCount;

	TextureInfo textureInfo
	{
		.textureFlags = {},
		.pcName = bIrradiance ? "GltfIrradiance" : "GltfPreFiltered",
		.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.format = vkFormat,
		.extent = VkExtent3D {static_cast<uint32_t>(iSize), static_cast<uint32_t>(iSize), 1},
		.mipLevels = static_cast<uint32_t>(iMipCount),
		.arrayLayers = 6,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kTransferDestination,
	};
	bIrradiance ? mGltfIrradianceTexture.Create(textureInfo) : mGltfPreFilteredTexture.Create(textureInfo);

	struct PushBlockIrradiance
	{
		shaders::vec4 f4x4ModelViewProjection[4] {};
		float fDeltaPhi = XM_2PI / 180.0f;
		float fDeltaTheta = XM_PIDIV2 / 64.0f;
	} pushBlockIrradiance;

	struct PushBlockPrefilterEnv
	{
		shaders::vec4 f4x4ModelViewProjection[4] {};
		float fRoughness = 0.0f;
		uint32_t uiNumSamples = 32;
	} pushBlockPrefilterEnv;

	XMMATRIX pMatrices[6] =
	{
		XMMatrixSet( 0.0f, 0.0f, -1.0f, 0.0f,   0.0f, -1.0f,  0.0f, 0.0f,   -1.0f,  0.0f,  0.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
		XMMatrixSet( 0.0f, 0.0f,  1.0f, 0.0f,   0.0f, -1.0f,  0.0f, 0.0f,    1.0f,  0.0f,  0.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
		XMMatrixSet( 1.0f, 0.0f,  0.0f, 0.0f,   0.0f,  0.0f, -1.0f, 0.0f,    0.0f,  1.0f,  0.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
		XMMatrixSet( 1.0f, 0.0f,  0.0f, 0.0f,   0.0f,  0.0f,  1.0f, 0.0f,    0.0f, -1.0f,  0.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
		XMMatrixSet( 1.0f, 0.0f,  0.0f, 0.0f,   0.0f, -1.0f,  0.0f, 0.0f,    0.0f,  0.0f, -1.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
		XMMatrixSet(-1.0f, 0.0f,  0.0f, 0.0f,   0.0f, -1.0f,  0.0f, 0.0f,    0.0f,  0.0f,  1.0f, 0.0f,    0.0f, 0.0f, 0.0f, 1.0f),
	};

	int64_t iFaceSize = iSize;
	for (int64_t i = 0; i < iMipCount; ++i, iFaceSize /= 2)
	{
		for (int64_t j = 0; j < 6; ++j)
		{
			Texture renderTargetTexture(
			{
				.textureFlags = {kRenderPass},
				.pcName = "GltfCubemap",
				.flags = 0,
				.format = vkFormat,
				.extent = VkExtent3D {static_cast<uint32_t>(iFaceSize), static_cast<uint32_t>(iFaceSize), 1},
				.mipLevels = 1,
				.arrayLayers = 1,
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.eTextureLayout = kColorAttachment,
			});

			Pipeline pipeline(
			{
				.pcName = "GltfCubemap",
				.flags = {PipelineFlags::kRenderTarget, PipelineFlags::kPushConstants},
				.uiPushConstantSize = static_cast<uint32_t>(bIrradiance ? sizeof(PushBlockIrradiance) : sizeof(PushBlockPrefilterEnv)),
				.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfFilterCubevertCrc), bIrradiance ? &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfIrradianceCubefragCrc) : &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfPrefilterEnvMapfragCrc)},
				.pVertexBuffer = &gpBufferManager->mModelMap.at(data::kGltfBoxBoxgltfGLTF_MODELCrc),
				.vkRenderPass = renderTargetTexture.mVkRenderPass,
				.vkExtent3D = renderTargetTexture.mInfo.extent,
				.pDescriptorInfos =
				{
					{.flags = DescriptorFlags::kCombinedSamplers, .iCount = 1, .pTexture = &gpTextureManager->mTextureMap.at(data::kTexturesCRyfjalletCrc)},
				},
			});

			OneShotCommandBuffer oneShotCommandBuffer;

			auto matPerspective = XMMatrixPerspectiveFovRH(XM_PIDIV2, 1.0f, 0.1f, 512.0f);
			if (bIrradiance)
			{
				XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&pushBlockIrradiance.f4x4ModelViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(pMatrices[j], matPerspective)));
			}
			else
			{
				XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&pushBlockPrefilterEnv.f4x4ModelViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(pMatrices[j], matPerspective)));
				pushBlockPrefilterEnv.fRoughness = static_cast<float>(i) / static_cast<float>(iMipCount - 1);
			}
			vkCmdPushConstants(oneShotCommandBuffer.mVkCommandBuffer, pipeline.mVkPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, bIrradiance ? sizeof(PushBlockIrradiance) : sizeof(PushBlockPrefilterEnv), bIrradiance ? static_cast<const void*>(&pushBlockIrradiance) : static_cast<const void*>(&pushBlockPrefilterEnv));

			renderTargetTexture.RecordBeginRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
			vkCmdBindPipeline(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.mVkPipeline);
			vkCmdBindDescriptorSets(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.mVkPipelineLayout, 0, 1, &pipeline.mVkDescriptorSets[0], 0, nullptr);
			pipeline.mInfo.pVertexBuffer->RecordBindVertexBuffer(oneShotCommandBuffer.mVkCommandBuffer);
			vkCmdDrawIndexed(oneShotCommandBuffer.mVkCommandBuffer, static_cast<uint32_t>(pipeline.mInfo.pVertexBuffer->mInfo.iCount), 1, 0, 0, 0);
			renderTargetTexture.RecordEndRenderPass(oneShotCommandBuffer.mVkCommandBuffer);

			VkImageCopy vkImageCopy
			{
				.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
				.srcOffset = {0, 0, 0},
				.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<uint32_t>(i), static_cast<uint32_t>(j), 1},
				.dstOffset = {0, 0, 0},
				.extent = {static_cast<uint32_t>(iFaceSize), static_cast<uint32_t>(iFaceSize), 1},
			};
			vkCmdCopyImage(oneShotCommandBuffer.mVkCommandBuffer, renderTargetTexture.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bIrradiance ? mGltfIrradianceTexture.mVkImage : mGltfPreFilteredTexture.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkImageCopy);

			oneShotCommandBuffer.Execute(true);
		}
	}

	OneShotCommandBuffer oneShotCommandBuffer;
	bIrradiance ? mGltfIrradianceTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly) : mGltfPreFilteredTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	oneShotCommandBuffer.Execute(true);
}

void TextureManager::GenerateGltfLutBrdf()
{
	mGltfLutBrdfTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "LutBrdf",
		.flags = 0,
		.format = VK_FORMAT_R16G16_SFLOAT,
		.extent = VkExtent3D {512, 512, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.renderPassInitialVkImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.renderPassFinalVkImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.eTextureLayout = kColorAttachment,
	});

	Pipeline pipeline(
	{
		.pcName = "GltfCubemap",
		.flags = {PipelineFlags::kRenderTarget},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfGenBrdfLutvertCrc), &gpShaderManager->mShaders.at(data::kShadersVulkanglTFPBRGltfGenBrdfLutfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = mGltfLutBrdfTexture.mVkRenderPass,
		.vkExtent3D = mGltfLutBrdfTexture.mInfo.extent,
		.pDescriptorInfos =
		{
		},
	});

	OneShotCommandBuffer oneShotCommandBuffer;

	mGltfLutBrdfTexture.RecordBeginRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
	pipeline.RecordDraw(0, oneShotCommandBuffer.mVkCommandBuffer, 1, 0);
	mGltfLutBrdfTexture.RecordEndRenderPass(oneShotCommandBuffer.mVkCommandBuffer);

	oneShotCommandBuffer.Execute(true);
}

} // namespace engine
