#if defined(BT_CLIENT)

#include "TextureManager.h"

#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"

#include "Data/Data.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

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
: mTextureDescriptors(*this)
{
	gpTextureManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerTextureManager);

	CreateSamplers();

	mRenderTargetTextures.Create();

	gpProfileManager->BootStart(kBootTimerTextureUpload);

	// Create 1x1 white placeholder textures for deferred texture loading
	mWhiteTexture.Create(
	{
		.textureFlags = {},
		.name = "WhitePlaceholder",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint32_t*>(pData) = 0xFFFFFFFF;
	});

	mWhiteCubeTexture.Create(
	{
		.textureFlags = {},
		.name = "WhiteCubePlaceholder",
		.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 6,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		uint32_t* pPixels = static_cast<uint32_t*>(pData);
		for (int64_t i = 0; i < 6; ++i)
		{
			pPixels[i] = 0xFFFFFFFF;
		}
	});

	// Slot-0 island placeholders. Format-matched to the bindless arrays; values chosen so
	// sampling slot 0 has no visible effect (sea-level elevation hidden by water rendering,
	// mid-gray color, up-vector normals, full-bright AO).
	mIslandPlaceholderElevation.Create(
	{
		.textureFlags = {},
		.name = "IslandPlaceholderElevation",
		.flags = 0,
		.format = VK_FORMAT_R32_SFLOAT,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<float*>(pData) = 0.0f;
	});

	mIslandPlaceholderColor.Create(
	{
		.textureFlags = {},
		.name = "IslandPlaceholderColor",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint32_t*>(pData) = 0xFF808080u;
	});

	mIslandPlaceholderNormals.Create(
	{
		.textureFlags = {},
		.name = "IslandPlaceholderNormals",
		.flags = 0,
		.format = VK_FORMAT_R8G8_UNORM,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint16_t*>(pData) = 0x8080u;
	});

	mIslandPlaceholderAmbientOcclusion.Create(
	{
		.textureFlags = {},
		.name = "IslandPlaceholderAmbientOcclusion",
		.flags = 0,
		.format = VK_FORMAT_R8_UNORM,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	},
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint8_t*>(pData) = 0xFFu;
	});

	// Create deferred textures from ChunkHeader metadata for all texture chunks (real GPU resources allocated when data arrives)
	for (auto& [rCrc, rLazyChunk] : gpFileManager->GetLazyChunkMap())
	{
		if (!(rLazyChunk.header.flags & common::ChunkFlags::kTexture))
		{
			continue;
		}

		bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;

		// Store metadata and point at white placeholder (no GPU allocation until data arrives)
		auto [it, bInserted] = mTextureMap.try_emplace(rCrc);
		ASSERT(bInserted);
		it->second.InitDeferred(TextureInfo
		{
			.textureFlags = {},
			.name = rLazyChunk.header.pcPath,
			.crc = rCrc,
			.flags = bCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : static_cast<VkImageCreateFlags>(0),
			.format = rLazyChunk.header.textureHeader.vkFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureWidth), static_cast<uint32_t>(rLazyChunk.header.textureHeader.iTextureHeight), 1},
			.mipLevels = static_cast<uint32_t>(rLazyChunk.header.textureHeader.iMipLevels),
			.arrayLayers = bCubemap ? 6u : 1u,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.viewType = bCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		}, bCubemap ? mWhiteCubeTexture.mVkImageView : mWhiteTexture.mVkImageView);
	}

	// Pre-fill texture arrays with white placeholders for lazy index assignment
	// Extra slots reserved for pre-blurred lighting texture copies
	static constexpr int64_t kiLightingBlurSlots = 16;
	mTextureDescriptors.mImageInfos.resize(mTextureMap.size() + kiLightingBlurSlots, {nullptr, mWhiteTexture.mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});

	mTextureDescriptors.Create();

	// Initialize island texture pointers sized to kiMaxIslands for shader descriptor arrays
	mRenderTargetTextures.mElevationTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mColorTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mNormalsTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mAmbientOcclusionTextures.resize(shaders::kiMaxIslands);

	// Island textures load dynamically per ClientDataReceiver::ApplyReceivedStaticData. Slot 0 is
	// a permanent neutral placeholder; higher slots alias slot 0 until AcquireTextureSlot binds a
	// real Texture* and RestorationSweep adopts the loaded chunks.
	for (int64_t i = 0; i < static_cast<int64_t>(shaders::kiMaxIslands); ++i)
	{
		mRenderTargetTextures.mElevationTextures[i] = &mIslandPlaceholderElevation;
		mRenderTargetTextures.mColorTextures[i] = &mIslandPlaceholderColor;
		mRenderTargetTextures.mNormalsTextures[i] = &mIslandPlaceholderNormals;
		mRenderTargetTextures.mAmbientOcclusionTextures[i] = &mIslandPlaceholderAmbientOcclusion;
	}

	gpProfileManager->BootStop(kBootTimerTextureUpload);

	// Create per-framebuffer command buffers for batched QFOT acquire barriers (before StartThread/WaitForTextures -> ProcessPendingTextures)
	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mAcquireVkCommandPool));

	uint32_t uiFramebufferCount = static_cast<uint32_t>(gpSwapchainManager->mFramebuffers.size());
	mAcquireVkCommandBuffers.resize(uiFramebufferCount);
	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mAcquireVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = uiFramebufferCount,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, mAcquireVkCommandBuffers.data()));

	gpProfileManager->BootStart(kModelTexturesGeneration);

	// Make sure to start the texture upload thread before WaitForTextures because it will wait on texture availability
	gpTextureUploadManager->StartThread();

	// Load pre-baked cubemaps from pack data
	static constexpr common::crc_t kIrradianceCrc = data::kTexturesCKloofendalPuresky_IrradianceR16G16B16A16_SFLOATCrc;
	static constexpr common::crc_t kPrefilteredCrc = data::kTexturesCKloofendalPuresky_PrefilteredR16G16B16A16_SFLOATCrc;
	static constexpr common::crc_t kPrefilteredWaterCrc = data::kTexturesCRyfjallet_PrefilteredR16G16B16A16_SFLOATCrc;
	common::crc_t pIblCrcs[] = {kIrradianceCrc, kPrefilteredCrc, kPrefilteredWaterCrc};
	WaitForTextures(pIblCrcs);
	mTextureCache.miPbrCubeMipCount = mTextureMap.at(kPrefilteredCrc).mInfo.mipLevels;

	gpProfileManager->BootStop(kModelTexturesGeneration);

	// Request priority textures
	gpFileManager->RequestChunkLoad(smPriorityTextures, LoadPriority::kRealtime);
}

TextureManager::~TextureManager()
{
	mTextureDescriptors.Destroy();

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mAcquireVkCommandPool, nullptr);

	DestroySamplers();
	mRenderTargetTextures.DestroyLightingTextures();

	gpTextureManager = nullptr;
}

void TextureManager::DestroyScreenDependentResources()
{
	mTextureDescriptors.Destroy();

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mAcquireVkCommandPool, nullptr);
	mAcquireVkCommandPool = VK_NULL_HANDLE;
	mAcquireVkCommandBuffers.clear();

	mRenderTargetTextures.DestroyLightingTextures();
}

void TextureManager::CreateScreenDependentResources()
{
	mRenderTargetTextures.Create();

	mTextureDescriptors.Create();

	VkCommandPoolCreateInfo vkCommandPoolCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = static_cast<uint32_t>(gpInstanceManager->miGraphicsQueueFamilyIndex),
	};
	CHECK_VK(vkCreateCommandPool(gpDeviceManager->mVkDevice, &vkCommandPoolCreateInfo, nullptr, &mAcquireVkCommandPool));

	uint32_t uiFramebufferCount = static_cast<uint32_t>(gpSwapchainManager->mFramebuffers.size());
	mAcquireVkCommandBuffers.resize(uiFramebufferCount);
	VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = mAcquireVkCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = uiFramebufferCount,
	};
	CHECK_VK(vkAllocateCommandBuffers(gpDeviceManager->mVkDevice, &vkCommandBufferAllocateInfo, mAcquireVkCommandBuffers.data()));
}

void TextureManager::DestroySamplers()
{
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerSmoke, nullptr);
	mVkSamplerSmoke = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerWindClamp, nullptr);
	mVkSamplerWindClamp = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerBorder, nullptr);
	mVkSamplerBorder = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerClamp, nullptr);
	mVkSamplerClamp = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerElevation, nullptr);
	mVkSamplerElevation = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerRepeat, nullptr);
	mVkSamplerRepeat = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerMirroredRepeat, nullptr);
	mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
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

	// Vulkan spec only mandates SAMPLED_IMAGE_FILTER_LINEAR_BIT for the 16-bit-float family. R32_SFLOAT
	// (used by smoke ping-pong and the per-island elevation heightmap) is optional; on devices without
	// the bit, sampling a R32_SFLOAT image with VK_FILTER_LINEAR is undefined per spec — silently aliased
	// or corrupted output with no validation message. Query once and downgrade the affected samplers to
	// VK_FILTER_NEAREST so the engine still boots; the warning surfaces in the launch log.
	const bool bR32SFloatLinearSupported = SupportsLinearFilter(VK_FORMAT_R32_SFLOAT);

	const VkFilter eSmokeFilter = bR32SFloatLinearSupported ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	// One-shot — CreateSamplers re-runs on anisotropy/lod-bias setting changes and device-lost recovery.
	static bool sbWarnedR32SFloatLinear = false;
	if (!bR32SFloatLinearSupported && !sbWarnedR32SFloatLinear)
	{
		LOG(kGraphics, kWarning, "VK_FORMAT_R32_SFLOAT does not advertise VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT — falling back to VK_FILTER_NEAREST for the smoke ping-pong and island-heightmap samplers. Smoke and terrain edges will appear blocky.\n");
		sbWarnedR32SFloatLinear = true;
	}

	VkSamplerCreateInfo smokeVkSamplerCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.magFilter = eSmokeFilter,
		.minFilter = eSmokeFilter,
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
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerSmoke, "Smoke");

	// Wind sampler: linear filtering for smooth advection + clamp-to-edge preserves energy at boundaries
	smokeVkSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	smokeVkSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	smokeVkSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	smokeVkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	smokeVkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	smokeVkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &smokeVkSamplerCreateInfo, nullptr, &mVkSamplerWindClamp));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerWindClamp, "WindClamp");

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
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerClamp, "Clamp");

	// Dedicated sampler for the per-island R32_SFLOAT heightmap (IslandTerrain bindless elevation array).
	// Mirrors mVkSamplerClamp settings but downgrades the filter to NEAREST when the device does not
	// advertise SAMPLED_IMAGE_FILTER_LINEAR_BIT for R32_SFLOAT. mVkSamplerClamp stays LINEAR so the
	// spec-mandated formats it also serves (BC7 color, BC5 normals, R8 AO) keep bilinear filtering.
	if (!bR32SFloatLinearSupported)
	{
		vkSamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
		vkSamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
	}
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerElevation));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerElevation, "Elevation");
	// Restore filters for subsequent Border/Repeat/MirroredRepeat samplers (they serve spec-mandated formats).
	vkSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	vkSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;

	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerBorder));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerBorder, "Border");
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerRepeat));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerRepeat, "Repeat");
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerMirroredRepeat));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerMirroredRepeat, "MirroredRepeat");
}

VkSampler TextureManager::GetSampler(DescriptorFlags_t flags)
{
	// Sampler flags are mutually exclusive — if a caller accidentally sets two, GetSampler's first-match order silently picks one and masks the bug.
	const int64_t iSamplerFlagCount = (flags & DescriptorFlags::kSamplerClamp ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerElevation ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerBorder ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerRepeat ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerMirroredRepeat ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerSmoke ? 1 : 0)
		+ (flags & DescriptorFlags::kSamplerWindClamp ? 1 : 0);
	ASSERT(iSamplerFlagCount <= 1);

	if (flags & DescriptorFlags::kSamplerElevation)
	{
		return mVkSamplerElevation;
	}
	else if (flags & DescriptorFlags::kSamplerClamp)
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
	else if (flags & DescriptorFlags::kSamplerWindClamp)
	{
		return mVkSamplerWindClamp;
	}
	else
	{
		return mVkSamplerClamp;
	}
}

void TextureManager::ProcessPendingTextures(int64_t iFramebufferIndex)
{
	mbHasPendingAcquireBarriers = false;
	miAcquireFramebufferIndex = iFramebufferIndex;
	bool bNeedAcquireBarrier = gpInstanceManager->miTransferQueueFamilyIndex != gpInstanceManager->miGraphicsQueueFamilyIndex;
	bool bRecordedBarriers = false;
	bool bAdoptedTextures = false;
	VkCommandBuffer vkAcquireCommandBuffer = mAcquireVkCommandBuffers.at(iFramebufferIndex);

	int64_t iAdoptedCount = 0;
	static constexpr int64_t kiMaxAdoptionsPerFrame = 4;

	for (auto& [rCrc, rTexture] : mTextureMap)
	{
		LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(rCrc);
		ChunkState eState = rLazyChunk.eState.load(std::memory_order_acquire);

		if (eState >= ChunkState::kReady)
		{
			continue;
		}

		if (eState == ChunkState::kGpuUploadComplete)
		{
			bool bFromTransferQueue = rLazyChunk.pData != nullptr;

			// Adopt the GPU-uploaded image (sets mVkImage and creates VkImageView)
			rTexture.AdoptTransferredImage(rLazyChunk.vkImage, rLazyChunk.vmaAllocation, rLazyChunk.vkDeviceMemory);

			bool bIsLightingTexture = mLightingTextureCrcs.contains(rCrc);
			bool bNeedsAcquire = bNeedAcquireBarrier && bFromTransferQueue;

			// Lighting textures handle their own acquire barrier inside BlurLightingTexture's OneShotCommandBuffer
			if (bNeedsAcquire && !bIsLightingTexture)
			{
				if (!bRecordedBarriers)
				{
					VkCommandBufferBeginInfo vkCommandBufferBeginInfo
					{
						.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
						.pNext = nullptr,
						.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
						.pInheritanceInfo = nullptr,
					};
					vkBeginCommandBuffer(vkAcquireCommandBuffer, &vkCommandBufferBeginInfo);
					bRecordedBarriers = true;
				}
				rTexture.RecordAcquireBarrier(vkAcquireCommandBuffer);
			}

			rLazyChunk.pData = nullptr;
			rLazyChunk.iDataSize = 0;
			mTextureDescriptors.UpdateDescriptorsForTexture(rCrc);
			bAdoptedTextures = true;

			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);

			if (bIsLightingTexture)
			{
				BlurLightingTexture(rCrc, bNeedsAcquire);
			}

			if (bFromTransferQueue && ++iAdoptedCount >= kiMaxAdoptionsPerFrame)
			{
				break;
			}
		}
		else if (eState == ChunkState::kDiskLoaded)
		{
			// Fallback: upload thread didn't GPU upload (same queue family)
			rTexture.Create(rTexture.mInfo, [&](void* pData, int64_t iPosition, int64_t iSize)
			{
				memcpy(pData, &rLazyChunk.pData[iPosition], iSize);
			});
			mTextureDescriptors.UpdateDescriptorsForTexture(rCrc);
			bAdoptedTextures = true;

			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);

			if (mLightingTextureCrcs.contains(rCrc))
			{
				BlurLightingTexture(rCrc);
			}

			// Only upload one texture a frame
			break;
		}
	}

	// Flush deferred texture array descriptor writes
	if (bAdoptedTextures)
	{
		mTextureDescriptors.UpdateTextureArrayDescriptors();
	}

	// Finalize acquire barrier command buffer for CommandBufferManager to prepend
	if (bRecordedBarriers)
	{
		vkEndCommandBuffer(vkAcquireCommandBuffer);
		mbHasPendingAcquireBarriers = true;
	}
}

void TextureManager::WaitForTextures(std::span<const common::crc_t> crcs)
{
	gpFileManager->RequestChunkLoad(crcs, LoadPriority::kRealtime);

	for (common::crc_t crc : crcs)
	{
		LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(crc);
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kReady)
		{
			continue;
		}

		// Upload in progress — spin until upload thread finishes and ProcessPendingTextures adopts
		while (rLazyChunk.eState.load(std::memory_order_acquire) < ChunkState::kReady)
		{
			// Signal upload thread to process one chunk (drain then release to avoid binary_semaphore double-release UB)
			// Return value intentionally discarded: we only need to drain the semaphore to 0 before release()
			std::ignore = gpTextureUploadManager->mFrameSignal.try_acquire();
			gpTextureUploadManager->mFrameSignal.release();

			std::this_thread::yield();
			ProcessPendingTextures(0);
		}
	}

	// Flush pending acquire barriers since we're not in the render loop
	if (mbHasPendingAcquireBarriers)
	{
		VkFenceCreateInfo vkFenceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
		};
		VkFence vkFence = VK_NULL_HANDLE;
		CHECK_VK(vkCreateFence(gpDeviceManager->mVkDevice, &vkFenceCreateInfo, nullptr, &vkFence));

		VkCommandBuffer vkAcquireCommandBuffer = mAcquireVkCommandBuffers.at(miAcquireFramebufferIndex);
		VkSubmitInfo vkSubmitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.pNext = nullptr,
			.waitSemaphoreCount = 0,
			.pWaitSemaphores = nullptr,
			.pWaitDstStageMask = nullptr,
			.commandBufferCount = 1,
			.pCommandBuffers = &vkAcquireCommandBuffer,
			.signalSemaphoreCount = 0,
			.pSignalSemaphores = nullptr,
		};
		CHECK_VK(vkQueueSubmit(gpDeviceManager->mGraphicsVkQueue, 1, &vkSubmitInfo, vkFence));
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &vkFence, VK_TRUE, UINT64_MAX));

		vkDestroyFence(gpDeviceManager->mVkDevice, vkFence, nullptr);
		mbHasPendingAcquireBarriers = false;
	}
}

void TextureManager::WaitForTextures(std::span<Texture* const> textures)
{
	common::ScopedWorkbufferArena scopedWorkbufferArena = common::gpThreadLocal->mWorkbuffer.Push();
	for (Texture* pTexture : textures)
	{
		common::gpThreadLocal->mWorkbuffer.PushBack<common::crc_t>(pTexture->mInfo.crc);
	}

	WaitForTextures(common::gpThreadLocal->mWorkbuffer.Span<common::crc_t>());
}

void RegisterLightingTextureCrc(common::crc_t crc)
{
	gpTextureManager->RegisterLightingTextureCrc(crc);
}

void TextureManager::RegisterLightingTextureCrc(common::crc_t crc)
{
	// Heap: unordered_set insert during startup registration
	ScopedSuppressAllocationTracking suppress;
	mLightingTextureCrcs.insert(crc);
}

void TextureManager::BlurLightingTexture(common::crc_t crc, bool bNeedAcquireBarrier)
{
	// Heap: GPU textures for pre-blurred lighting
	ScopedSuppressAllocationTracking suppress;

	Texture& rSource = mTextureMap.at(crc);
	uint32_t uiWidth = rSource.mInfo.extent.width * 2;
	uint32_t uiHeight = rSource.mInfo.extent.height * 2;

	// Create or recreate intermediate texture
	auto [itIntermediate, bInsertedIntermediate] = mBlurIntermediateTextures.try_emplace(crc);
	if (!bInsertedIntermediate)
	{
		itIntermediate->second.Destroy();
	}
	itIntermediate->second.Create(
	{
		.textureFlags = {},
		.name = "LightingBlurIntermediate",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {uiWidth, uiHeight, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kComputeReadWrite,
	});

	// Create or recreate result texture
	auto [itResult, bInsertedResult] = mBlurredLightingTextures.try_emplace(crc);
	if (!bInsertedResult)
	{
		itResult->second.Destroy();
	}
	itResult->second.Create(
	{
		.textureFlags = {},
		.name = "LightingBlurResult",
		.flags = 0,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = VkExtent3D {uiWidth, uiHeight, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	});

	Texture& rIntermediate = itIntermediate->second;
	Texture& rResult = itResult->second;

	// Update pipeline descriptors for this blur pass
	Pipeline& rBlurH = gpPipelineManager->mpPipelines[kPipelineLightingBlurH];
	Pipeline& rBlurV = gpPipelineManager->mpPipelines[kPipelineLightingBlurV];

	rBlurH.UpdateCombinedImageSamplerDescriptor(0, rSource.mVkImageView, mVkSamplerClamp);
	rBlurH.UpdateStorageImageDescriptor(1, rIntermediate.mVkImageView);
	rBlurV.UpdateCombinedImageSamplerDescriptor(0, rIntermediate.mVkImageView, mVkSamplerClamp);
	rBlurV.UpdateStorageImageDescriptor(1, rResult.mVkImageView);

	// Execute blur via one-shot command buffer
	int32_t iWidth = static_cast<int32_t>(uiWidth);
	int32_t iHeight = static_cast<int32_t>(uiHeight);
	float fSigma = gLightingBlurSigma.Get();
	float fPackedW = static_cast<float>(static_cast<int32_t>(gLightingBlurSampleCount.Get())) + gLightingBlurEdgeFalloff.Get() / 100.0f;

	OneShotCommandBuffer oneShotCommandBuffer;
	VkCommandBuffer vkCommandBuffer = oneShotCommandBuffer.mVkCommandBuffer;

	// Complete queue family ownership transfer if texture was uploaded on a separate transfer queue
	if (bNeedAcquireBarrier)
	{
		rSource.RecordAcquireBarrier(vkCommandBuffer);
	}

	// Horizontal pass: source → intermediate
	rIntermediate.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kComputeReadWrite);
	rBlurH.RecordCompute(0, vkCommandBuffer, (uiWidth + 7) / 8, (uiHeight + 7) / 8, 1, {std::bit_cast<float>(iWidth), std::bit_cast<float>(iHeight), fSigma, fPackedW});

	// Transition intermediate: storage write → shader read for V pass sampler
	rIntermediate.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// Vertical pass: intermediate → result
	rResult.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	rBlurV.RecordCompute(0, vkCommandBuffer, (uiWidth + 7) / 8, (uiHeight + 7) / 8, 1, {std::bit_cast<float>(iWidth), std::bit_cast<float>(iHeight), fSigma, fPackedW});

	// Transition result back to shader read for bindless sampling
	rResult.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	oneShotCommandBuffer.Execute(true);

	// Register blurred texture in bindless array
	static constexpr common::crc_t kBlurSalt = 0x424C5552; // "BLUR"
	common::crc_t blurredCrc = crc ^ kBlurSalt;
	int64_t iBlurredIndex = static_cast<int64_t>(mTextureDescriptors.CrcToIndex(blurredCrc));
	mTextureDescriptors.mImageInfos.at(iBlurredIndex).imageView = rResult.mVkImageView;
	mTextureDescriptors.UpdateTextureArrayDescriptors();
}

void TextureManager::ReblurAllLightingTextures()
{
	for (common::crc_t crc : mLightingTextureCrcs)
	{
		LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(crc);
		if (rLazyChunk.eState.load(std::memory_order_acquire) >= ChunkState::kReady)
		{
			BlurLightingTexture(crc);
		}
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
