#if defined(BT_CLIENT)

#include "TextureManager.h"

#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/PbrWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

using enum TextureFlags;
using enum TextureLayout;

// Extra texture-descriptor slots reserved for pre-blurred lighting texture copies (one per registered lighting texture CRC)
static constexpr int64_t kiLightingBlurSlots = 16;

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

std::tuple<int64_t, int64_t> TextureManager::LightingDetailTextureSize(float fMultiplier)
{
	// Pre-size every lighting deposit/spread/combine texture by Camera::kfLightingHeadroomMultiplier so the constant
	// on-screen-pixel-size texel grid retains coverage margin while its camera-height reference expands immediately
	// outward and contracts gradually inward. Centralized so all lighting consumers stay byte-consistent
	// (deposit quads must land on the same texels the area math snaps to). Clamp AFTER the multiply (DetailTextureSize
	// clamps pre-multiply); force width even so downstream half-width math stays integer.
	auto [iBaseX, iBaseY] = DetailTextureSize(fMultiplier);
	int64_t iLimit = static_cast<int64_t>(gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxImageDimension2D);
	int64_t iX = std::min(static_cast<int64_t>(std::lround(static_cast<float>(iBaseX) * game::Camera::kfLightingHeadroomMultiplier)), iLimit);
	iX &= ~1ll;
	int64_t iY = std::min(static_cast<int64_t>(std::lround(static_cast<float>(iBaseY) * game::Camera::kfLightingHeadroomMultiplier)), iLimit);
	return std::make_tuple(iX, iY);
}

std::tuple<int64_t, int64_t> TextureManager::WaterDetailTextureSize(float fMultiplier)
{
	auto [iWorldDetailX, iWorldDetailY] = WaterFullDetail();

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

void TextureManager::CreatePlaceholderTexture(Texture& rTexture, std::string_view name, VkImageCreateFlags vkImageCreateFlags, VkFormat vkFormat, uint32_t uiArrayLayers, VkImageViewType vkImageViewType, const std::function<void(void*, int64_t, int64_t)>& rPixelWriter)
{
	rTexture.Create(
	{
		.textureFlags = {},
		.name = name,
		.flags = vkImageCreateFlags,
		.format = vkFormat,
		.extent = VkExtent3D {1, 1, 1},
		.mipLevels = 1,
		.arrayLayers = uiArrayLayers,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = vkImageViewType,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	}, rPixelWriter);
}

TextureManager::TextureManager()
: mTextureDescriptors(*this)
{
	ASSERT(gpTextureManager == nullptr);

	gpTextureManager = this;

	ScopedBootTimer scopedBootTimer(kBootTimerTextureManager);

	CreateSamplers();

	mRenderTargetTextures.Create();

	gpProfileManager->BootStart(kBootTimerTextureUpload);

	// Create 1x1 white placeholder textures for deferred texture loading
	CreatePlaceholderTexture(mWhiteTexture, "WhitePlaceholder", 0, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint32_t*>(pData) = 0xFFFFFFFF;
	});

	CreatePlaceholderTexture(mWhiteCubeTexture, "WhiteCubePlaceholder", VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_FORMAT_R8G8B8A8_UNORM, 6, VK_IMAGE_VIEW_TYPE_CUBE,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		uint32_t* pPixels = static_cast<uint32_t*>(pData);
		for (int64_t i = 0; i < 6; ++i)
		{
			pPixels[i] = 0xFFFFFFFF;
		}
	});

	// Slot-0 island placeholders. Format-matched to the bindless arrays; values chosen so
	// sampling slot 0 has no visible effect (ocean-bottom elevation submerged below the water,
	// mid-gray color, up-vector normals, full-bright AO).
	CreatePlaceholderTexture(mIslandPlaceholderElevation, "IslandPlaceholderElevation", 0, shaders::keElevationFormat, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		// Ocean-bottom, matching the elevation RTT clear (RenderTargetTextures.cpp) and the
		// open-ocean CPU floor (IslandTerrain::mfSeaFloorElevation). Island slots that are not yet
		// GPU-resident (startup, mid-load before RestorationSweep, evicted-slot grace window) alias
		// this placeholder; ocean-bottom keeps their footprint submerged under the water instead of
		// rendering a sea-level plane that pokes through the surface.
		*static_cast<uint16_t*>(pData) = DirectX::PackedVector::XMConvertFloatToHalf(gpIslandTerrain->mfSeaFloorElevation);
	});

	CreatePlaceholderTexture(mIslandPlaceholderColor, "IslandPlaceholderColor", 0, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint32_t*>(pData) = 0xFF808080u;
	});

	CreatePlaceholderTexture(mIslandPlaceholderNormals, "IslandPlaceholderNormals", 0, VK_FORMAT_R8G8_UNORM, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint16_t*>(pData) = 0x8080u;
	});

	CreatePlaceholderTexture(mIslandPlaceholderAmbientOcclusion, "IslandPlaceholderAmbientOcclusion", 0, VK_FORMAT_R8_UNORM, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint8_t*>(pData) = 0xFFu;
	});

	// All-zero RGBA: no rock/sand/snow/flow until the real BC7 mask chunk adopts. Bindless arrays
	// don't require uniform format across slots, so R8G8B8A8 here while real masks are BC7 is OK
	// (same precedent as mIslandPlaceholderColor above vs BC7 islands).
	CreatePlaceholderTexture(mIslandPlaceholderMasks, "IslandPlaceholderMasks", 0, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_IMAGE_VIEW_TYPE_2D,
	[](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		*static_cast<uint32_t*>(pData) = 0x00000000u;
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

		// Water normal maps: copy the DataPacker-baked per-mip Toksvig variance table (already
		// padded past the real mip chain with the last value) for the WATER_SPEC_AA_MIP_HANDOFF
		// uniform upload. Header-resident so no lazy chunk data is needed at startup.
		static_assert(shaders::kiWaterSpecAAMipTableSize == common::TextureHeader::kiMipVarianceCount, "The shader-side mip-variance table length must match the pack format's");
		for (int64_t i = 0; i < kiWaterNormalCount; ++i)
		{
			if (kpWaterNormalCrcs[i] == rCrc)
			{
				std::memcpy(mpfWaterNormalMipVariance[i], rLazyChunk.header.textureHeader.pfMipVariance, sizeof(mpfWaterNormalMipVariance[i]));
				break;
			}
		}
	}

	// Pre-fill texture arrays with white placeholders for lazy index assignment
	// Extra slots reserved for pre-blurred lighting texture copies
	mTextureDescriptors.mImageInfos.resize(mTextureMap.size() + kiLightingBlurSlots, {nullptr, mWhiteTexture.mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});

	mTextureDescriptors.Create();

	// Initialize island texture pointers sized to kiMaxIslands for shader descriptor arrays.
	// .data() pointer-stability across this manager's lifetime is load-bearing: each array's .data()
	// pointer is the map key in TextureDescriptors::mBindlessArrayConsumers. Do not re-resize these
	// vectors after this point — a re-resize would dangle the registry keys.
	mRenderTargetTextures.mElevationTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mColorTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mNormalsTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mAmbientOcclusionTextures.resize(shaders::kiMaxIslands);
	mRenderTargetTextures.mMasksTextures.resize(shaders::kiMaxIslands);

	// Device-lost recovery: clear per-template slot residency state so the next AcquireTextureSlot
	// runs the first-mint path, re-registering all five channel bindings while elevation remains at
	// its placeholder until the four chunk-backed channels are ready. Without this, every island
	// would silently stay on the placeholder set up by the fan-out loop below — see
	// Graphics/DynamicIslandLoadingFollowups.md Follow-up 1.
	gpIslandTerrain->ResetTextureSlots();

	// Island textures load dynamically per ClientSession::ApplyReceivedStaticData. TextureDescriptors
	// owns the slot writes; these fixed vectors keep the stable backing addresses it registers.
	mTextureDescriptors.InitializeIslandSlots();

	gpProfileManager->BootStop(kBootTimerTextureUpload);

	// Create per-framebuffer command buffers for batched QFOT acquire barriers (before InitializeBootTextures -> ProcessPendingTextures)
	CreateAcquireCommandBuffers();
}

void TextureManager::InitializeBootTextures()
{
	gpProfileManager->BootStart(kModelTexturesGeneration);

	// Make sure to start the texture upload thread before WaitForTextures because it will wait on texture availability
	gpTextureUploadManager->StartThread();

	// Load pre-baked cubemaps from pack data
	common::crc_t pIblCrcs[] = {kIrradianceCrc, kPrefilteredCrc, kPrefilteredWaterCrc};
	WaitForTextures(pIblCrcs);
	mTextureCache.miPbrCubeMipCount = mTextureMap.at(kPrefilteredCrc).mInfo.mipLevels;

	gpProfileManager->BootStop(kModelTexturesGeneration);

	// Request priority textures
	gpFileManager->RequestChunkLoad(kpPriorityTextures.pCrcs, LoadPriority::kRealtime);
}

TextureManager::~TextureManager()
{
	mTextureDescriptors.Destroy();

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mAcquireVkCommandPool, nullptr);

	DestroySamplers();
	mRenderTargetTextures.DestroyLightingTextures();

	if (gpTextureManager == this)
	{
		gpTextureManager = nullptr;
	}
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

	CreateAcquireCommandBuffers();
}

void TextureManager::CreateAcquireCommandBuffers()
{
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
	for (VkSampler& rVkSampler : mpSamplers)
	{
		vkDestroySampler(gpDeviceManager->mVkDevice, rVkSampler, nullptr);
		rVkSampler = VK_NULL_HANDLE;
	}
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
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &smokeVkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotSmoke]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotSmoke], "Smoke");

	// Wind sampler: linear filtering for smooth advection + clamp-to-edge preserves energy at boundaries
	smokeVkSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	smokeVkSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	smokeVkSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	smokeVkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	smokeVkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	smokeVkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &smokeVkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotWindClamp]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotWindClamp], "WindClamp");

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
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotClamp]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotClamp], "Clamp");

	// Dedicated sampler for the per-island R16_SFLOAT heightmap (IslandTerrain bindless elevation array).
	// Mirrors mpSamplers[kSamplerSlotClamp]; R16_SFLOAT linear filtering is spec-mandated (16-bit-float family),
	// so this stays LINEAR unconditionally.
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotElevation]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotElevation], "Elevation");

	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotBorder]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotBorder], "Border");
	// White border (opaque 1.0): the shadow texture is inverse (1.0 = fully lit / no shadow), so any sample beyond the
	// texture extent reads "no shadow" instead of smearing the edge.
	vkSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotBorderWhite]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotBorderWhite], "BorderWhite");
	vkSamplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotRepeat]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotRepeat], "Repeat");

	// Model normal and metallic-roughness textures carry data rather than color. Apply their dedicated
	// bias directly so negative sharpens, positive blurs, and zero is unbiased.
	vkSamplerCreateInfo.mipLodBias = std::clamp(gPbrModelDataMipLodBias.Get(), -gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias, gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias);
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotRepeatModelData]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotRepeatModelData], "RepeatModelData");

	vkSamplerCreateInfo.mipLodBias = -gMipLodBias.Get();
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotMirroredRepeat]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotMirroredRepeat], "MirroredRepeat");

	// Water-normal variant: its own slider-driven bias instead of the global -gMipLodBias sharpen —
	// a sharpen bias tuned for albedo pushes minified normal fetches toward noisier mips (specular
	// shimmer), and Water.frag's WATER_SPEC_AA_MIP_HANDOFF analytic LOD must track the hardware LOD
	// (the slider value is also uploaded as fWaterNormalMipBias). Applied directly, not negated:
	// negative = sharpen, positive = blur.
	vkSamplerCreateInfo.mipLodBias = std::clamp(gWaterNormalMipBias.Get(), -gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias, gpInstanceManager->mVkPhysicalDeviceProperties.limits.maxSamplerLodBias);
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mpSamplers[kSamplerSlotMirroredRepeatWater]));
	VkName(VK_OBJECT_TYPE_SAMPLER, mpSamplers[kSamplerSlotMirroredRepeatWater], "MirroredRepeatWater");
}

VkSampler TextureManager::GetSampler(DescriptorFlags_t flags)
{
	// Maps each sampler flag to its slot. Order matches the prior first-match if/else chain (Elevation
	// first); flags are mutually exclusive, so order only matters if a caller violates that (asserted below).
	static constexpr struct { DescriptorFlags flag; SamplerSlot slot; } kFlagToSlot[]
	{
		{DescriptorFlags::kSamplerElevation, kSamplerSlotElevation},
		{DescriptorFlags::kSamplerClamp, kSamplerSlotClamp},
		{DescriptorFlags::kSamplerBorder, kSamplerSlotBorder},
		{DescriptorFlags::kSamplerBorderWhite, kSamplerSlotBorderWhite},
		{DescriptorFlags::kSamplerRepeat, kSamplerSlotRepeat},
		{DescriptorFlags::kSamplerMirroredRepeat, kSamplerSlotMirroredRepeat},
		{DescriptorFlags::kSamplerMirroredRepeatWater, kSamplerSlotMirroredRepeatWater},
		{DescriptorFlags::kSamplerSmoke, kSamplerSlotSmoke},
		{DescriptorFlags::kSamplerWindClamp, kSamplerSlotWindClamp},
	};

	// Sampler flags are mutually exclusive — if a caller accidentally sets two, the first table match silently picks one and masks the bug.
	int64_t iSamplerFlagCount = 0;
	for (const auto& rEntry : kFlagToSlot)
	{
		iSamplerFlagCount += (flags & rEntry.flag ? 1 : 0);
	}
	ASSERT(iSamplerFlagCount <= 1);

	for (const auto& rEntry : kFlagToSlot)
	{
		if (flags & rEntry.flag)
		{
			return mpSamplers[rEntry.slot];
		}
	}

	// No sampler flag set — default to clamp (matches the prior else branch).
	return mpSamplers[kSamplerSlotClamp];
}

void TextureManager::ProcessPendingTextures(int64_t iFramebufferIndex)
{
	mbHasPendingAcquireBarriers = false;

	// Idle-frame fast path: skip the full mTextureMap scan when nothing is in an adoptable state. The
	// TextureUploadManager pending-adoption counter is armed when a chunk reaches kDiskLoaded/kGpuUploadComplete
	// and disarmed below at adoption, so a zero count means no chunk can be adopted this frame.
	if (!gpTextureUploadManager->HasPendingAdoptions())
	{
		return;
	}

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
			AdoptUploadedChunk(rCrc, rTexture, bNeedAcquireBarrier, vkAcquireCommandBuffer, bRecordedBarriers);
			bAdoptedTextures = true;

			if (++iAdoptedCount >= kiMaxAdoptionsPerFrame)
			{
				break;
			}
		}
		else if (eState == ChunkState::kDiskLoaded)
		{
			// Fallback: upload thread didn't GPU upload (same queue family)

			// Trust boundary: the fallback copy loop below sizes its memcpy off rLazyChunk.pData using the on-disk
			// TextureHeader dims stored in rTexture.mInfo. The upload thread bounds those via ValidateTextureDimensions
			// before uploading, but the same-queue-family / no-transfer-pool early-out (HandleUploadEarlyOut) reaches
			// kDiskLoaded unvalidated, so bound here too. Mirror the upload thread's iDataSize check; on violation
			// soft-fail the chunk to kReady (leaving the borrowed white placeholder) rather than read off pData.
			int64_t iExpectedBytes = common::ComputeImageByteSize(rTexture.mInfo.format, rTexture.mInfo.extent.width, rTexture.mInfo.extent.height, rTexture.mInfo.mipLevels, rTexture.mInfo.arrayLayers, rTexture.mInfo.extent.depth);
			if (iExpectedBytes <= 0 || iExpectedBytes > rLazyChunk.iDataSize)
			{
				LOG(kLoading, kError, "Corrupt texture chunk {}: expected {} bytes exceeds chunk data {}; marking ready without adopt", rCrc, iExpectedBytes, rLazyChunk.iDataSize);
				DEBUG_BREAK();
				rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
				gpTextureUploadManager->NotifyChunkAdopted(); // adoptable -> kReady: disarm the pending-adoption counter armed when the chunk reached kDiskLoaded
				continue;
			}

			rTexture.Create(rTexture.mInfo, [&](void* pData, int64_t iPosition, int64_t iSize)
			{
				std::memcpy(pData, &rLazyChunk.pData[iPosition], iSize);
			});
			mTextureDescriptors.UpdateDescriptorsForTexture(rCrc);
			bAdoptedTextures = true;

			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
			gpTextureUploadManager->NotifyChunkAdopted(); // adoptable -> kReady: disarm the pending-adoption counter

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

	// Finalize acquire barrier command buffer for CommandBufferManager to prepend. mbHasPendingAcquireBarriers and
	// miAcquireFramebufferIndex (set earlier in this function) are plain non-atomic members written here on the main
	// thread (ProcessPendingTextures runs from Graphics::RenderGlobal) and read on the mSubmitGlobal worker in
	// CommandBufferManager::SubmitGlobalToQueue — the publish is ordered only by that worker's Wake/Wait edge. Same
	// "plain member published across a PersistentWorker Wake/Wait edge" family as CommandBuffers.h (mFlags/mVkFence).
	if (bRecordedBarriers)
	{
		CHECK_VK(vkEndCommandBuffer(vkAcquireCommandBuffer));
		mbHasPendingAcquireBarriers = true;
	}
}

void TextureManager::AdoptUploadedChunk(common::crc_t crc, Texture& rTexture, bool bNeedAcquireBarrier, VkCommandBuffer vkAcquireCommandBuffer, bool& brRecordedBarriers)
{
	LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(crc);

	// Adopt the GPU-uploaded image (sets mVkImage and creates VkImageView)
	rTexture.AdoptTransferredImage(rLazyChunk.vkImage, rLazyChunk.vmaAllocation);

	bool bIsLightingTexture = mLightingTextureCrcs.contains(crc);

	// Lighting textures handle their own acquire barrier inside BlurLightingTexture's OneShotCommandBuffer
	if (bNeedAcquireBarrier && !bIsLightingTexture)
	{
		EnsureAcquireCommandBufferBegun(vkAcquireCommandBuffer, brRecordedBarriers);
		rTexture.RecordAcquireBarrier(vkAcquireCommandBuffer);
	}

	// Race-free null of worker-thread-shared CPU-pool state: reaching kGpuUploadComplete means
	// the transfer thread (UploadThread) finished this chunk and released ownership, so nulling
	// pData here (on the main thread) cannot race the transfer thread — the same ordering
	// invariant FileManager::ResetTextureChunkStates documents.
	gpFileManager->DecommitChunkRange(crc, 0, rLazyChunk.iDataSize);
	rLazyChunk.pData = nullptr;
	rLazyChunk.iDataSize = 0;
	mTextureDescriptors.UpdateDescriptorsForTexture(crc);

	rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);
	gpTextureUploadManager->NotifyChunkAdopted(); // adoptable -> kReady: disarm the pending-adoption counter

	if (bIsLightingTexture)
	{
		BlurLightingTexture(crc, bNeedAcquireBarrier);
	}
}

void TextureManager::EnsureAcquireCommandBufferBegun(VkCommandBuffer vkAcquireCommandBuffer, bool& brRecordedBarriers)
{
	if (brRecordedBarriers)
	{
		return;
	}

	VkCommandBufferBeginInfo vkCommandBufferBeginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};
	CHECK_VK(vkBeginCommandBuffer(vkAcquireCommandBuffer, &vkCommandBufferBeginInfo));
	brRecordedBarriers = true;
}

bool TextureManager::AnyAdoptionPending() const
{
	// True when any chunk sits in an adoptable state (kGpuUploadComplete: transfer-queue uploaded, awaiting adopt;
	// or kDiskLoaded: same-queue-family / re-armed fallback, adopted via Create). On such a frame
	// ProcessPendingTextures writes descriptor elements (UpdateDescriptorsForTexture per-slot, the
	// UpdateTextureArrayDescriptors flush, and the lighting-blur array write), so RenderGlobal's all-framebuffer-fence
	// drain must fire first. The TextureUploadManager pending-adoption counter tracks exactly those two states
	// (kUploading, between them, is excluded), replacing a full per-frame mTextureMap scan with an O(1) read.
	return gpTextureUploadManager->HasPendingAdoptions();
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
			gpTextureUploadManager->RethrowException();

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
	// A 17th lighting texture would overflow the reserved blur slots (TextureDescriptors.cpp's generic index ASSERT fires
	// later and elsewhere); fail at the cause, naming the constant
	ASSERT(static_cast<int64_t>(mLightingTextureCrcs.size()) <= kiLightingBlurSlots);
}

void TextureManager::BlurLightingTexture(common::crc_t crc, bool bNeedAcquireBarrier)
{
	// Heap: GPU textures for pre-blurred lighting
	ScopedSuppressAllocationTracking suppress;

	Texture& rSource = mTextureMap.at(crc);
	uint32_t uiWidth = rSource.mInfo.extent.width * 2;
	uint32_t uiHeight = rSource.mInfo.extent.height * 2;

	// Create or recreate intermediate texture (Texture::Create self-destroys any prior image)
	auto itIntermediate = mBlurIntermediateTextures.try_emplace(crc).first;
	itIntermediate->second.Create(
	{
		.textureFlags = {},
		.name = "LightingBlurIntermediate",
		.flags = 0,
		.format = shaders::keCombineFormat,
		.extent = VkExtent3D {uiWidth, uiHeight, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kComputeReadWrite,
	});

	// Create or recreate result texture (Texture::Create self-destroys any prior image)
	auto itResult = mBlurredLightingTextures.try_emplace(crc).first;
	itResult->second.Create(
	{
		.textureFlags = {},
		.name = "LightingBlurResult",
		.flags = 0,
		.format = shaders::keCombineFormat,
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

	rBlurH.UpdateCombinedImageSamplerDescriptor(0, rSource.mVkImageView, mpSamplers[kSamplerSlotClamp]);
	rBlurH.UpdateStorageImageDescriptor(1, rIntermediate.mVkImageView);
	rBlurV.UpdateCombinedImageSamplerDescriptor(0, rIntermediate.mVkImageView, mpSamplers[kSamplerSlotClamp]);
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
	rBlurH.RecordCompute(0, vkCommandBuffer, TileCount(uiWidth), TileCount(uiHeight), 1, {std::bit_cast<float>(iWidth), std::bit_cast<float>(iHeight), fSigma, fPackedW});

	// Transition intermediate: storage write → shader read for V pass sampler
	rIntermediate.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	// Vertical pass: intermediate → result
	rResult.TransitionImageLayout(vkCommandBuffer, kShaderReadOnly, kComputeReadWrite);
	rBlurV.RecordCompute(0, vkCommandBuffer, TileCount(uiWidth), TileCount(uiHeight), 1, {std::bit_cast<float>(iWidth), std::bit_cast<float>(iHeight), fSigma, fPackedW});

	// Transition result back to shader read for bindless sampling
	rResult.TransitionImageLayout(vkCommandBuffer, kComputeReadWrite, kShaderReadOnly);

	oneShotCommandBuffer.Execute();

	// Register blurred texture in bindless array. Render-phase caller: CrcToIndex is lock-free, safe only
	//   because no worker Spawn runs concurrently (see TextureDescriptors::CrcToIndex).
	ASSERT(common::gpThreadLocal == nullptr || !common::gpThreadLocal->mbInFrameTick);
	common::crc_t blurredCrc = crc ^ TextureDescriptors::kBlurSalt;
	int64_t iBlurredIndex = mTextureDescriptors.CrcToIndex(blurredCrc);
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
