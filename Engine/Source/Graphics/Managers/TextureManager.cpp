#include "TextureManager.h"

#include "File/FileManager.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Graphics/OneShotCommandBuffer.h"
#include "Profile/ProfileManager.h"
#include "Frame/Pools/Smoke.h"

#include "Frame/Frame.h"


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

// Helper function for copying image data from GPU to CPU
void TextureManager::CopyImageToHostMemory(VkImage srcImage, VkExtent3D extent, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool bFromSwapchain, std::vector<std::byte>& outData)
{
	VkImageLayout currentLayout = bFromSwapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkImageLayout restoreLayout = bFromSwapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkPipelineStageFlags srcStage = bFromSwapchain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkPipelineStageFlags dstStage = bFromSwapchain ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkAccessFlags srcAccess = bFromSwapchain ? 0 : VK_ACCESS_SHADER_READ_BIT;
	VkAccessFlags dstAccess = bFromSwapchain ? 0 : VK_ACCESS_SHADER_READ_BIT;

	// Calculate total data size
	int64_t iMipWidth = extent.width;
	int64_t iMipHeight = extent.height;
	int64_t iTotalSize = 0;
	for (uint32_t i = 0; i < mipLevels; ++i)
	{
		iTotalSize += common::SizeInBytes(format, iMipWidth, iMipHeight) * arrayLayers;
		iMipWidth = std::max(iMipWidth / 2, 1ll);
		iMipHeight = std::max(iMipHeight / 2, 1ll);
	}

	// Allocate output data
	outData.resize(iTotalSize);

	// Create staging buffer
	VkBuffer stagingVkBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingVkDeviceMemory = VK_NULL_HANDLE;
	VmaAllocation stagingVmaAllocation = VK_NULL_HANDLE;
	VmaAllocationInfo stagingVmaAllocationInfo {};
	Buffer::CreateBuffer("ImageCopyStaging", iTotalSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVkBuffer, stagingVkDeviceMemory, stagingVmaAllocation, &stagingVmaAllocationInfo);

	OneShotCommandBuffer oneShotCommandBuffer;

	// Transition image to transfer source layout
	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = srcAccess,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = currentLayout,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = srcImage,
		.subresourceRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = mipLevels,
			.baseArrayLayer = 0,
			.layerCount = arrayLayers,
		},
	};
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);

	// Copy each mip level and array layer to staging buffer
	iMipWidth = extent.width;
	iMipHeight = extent.height;
	size_t uiOffset = 0;

	for (uint32_t iLayer = 0; iLayer < arrayLayers; ++iLayer)
	{
		iMipWidth = extent.width;
		iMipHeight = extent.height;
		for (uint32_t iMip = 0; iMip < mipLevels; ++iMip)
		{
			VkBufferImageCopy vkBufferImageCopy
			{
				.bufferOffset = uiOffset,
				.imageSubresource =
				{
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = iMip,
					.baseArrayLayer = iLayer,
					.layerCount = 1,
				},
				.imageExtent =
				{
					.width = static_cast<uint32_t>(iMipWidth),
					.height = static_cast<uint32_t>(iMipHeight),
					.depth = 1,
				},
			};

			vkCmdCopyImageToBuffer(oneShotCommandBuffer.mVkCommandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingVkBuffer, 1, &vkBufferImageCopy);

			uiOffset += common::SizeInBytes(format, iMipWidth, iMipHeight);
			iMipWidth = std::max(iMipWidth / 2, 1ll);
			iMipHeight = std::max(iMipHeight / 2, 1ll);
		}
	}

	// Transition image back to original layout
	vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	vkImageMemoryBarrier.newLayout = restoreLayout;
	vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkImageMemoryBarrier.dstAccessMask = dstAccess;
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);

	oneShotCommandBuffer.Execute(true);

	// Use VMA's pre-mapped pointer to copy data to output
	ASSERT(stagingVmaAllocationInfo.pMappedData != nullptr);
	memcpy(outData.data(), stagingVmaAllocationInfo.pMappedData, iTotalSize);

	// Cleanup staging buffer
	vmaDestroyBuffer(gpDeviceManager->mpAllocator, stagingVkBuffer, stagingVmaAllocation);
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

	BOOT_TIMER_START(kBootTimerTextureUpload);
	// You need to manually change kiTextureCount/kiUiTextureCount in ShaderLayoutsBase.h to match the same values in Data.h
	static_assert(data::kiTextureCount == shaders::kiTextureCount);
	static_assert(data::kiUiTextureCount == shaders::kiUiTextureCount);
	// DT: TODO In tools, can export textures before shaders, generate a texture header, then compile shaders after?

	// Create pre-sized empty textures from ChunkHeader metadata for all texture chunks (these will be updated in-place when actual data is loaded)
	for (auto& [rCrc, rLazyChunk] : gpFileManager->GetLazyChunkMap())
	{
		if (!(rLazyChunk.header.flags & common::ChunkFlags::kTexture))
		{
			continue;
		}

		bool bCubemap = rLazyChunk.header.flags & common::ChunkFlags::kCubemap;

		// Create empty texture with correct dimensions and format
		auto [it, bInserted] = mTextureMap.try_emplace(rCrc, TextureInfo
		{
			.textureFlags = {},
			.pcName = rLazyChunk.header.pcPath,
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
		});
		ASSERT(bInserted);
	}

	// Build texture array indices from hardcoded CRC lists (defines shader binding order)
	for (int64_t i = 0; const common::crc_t& rCrc : data::kpTextureCrcs)
	{
		mImageInfos.emplace_back(nullptr, mTextureMap.at(rCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		mImageInfosMap.try_emplace(rCrc, i++);
	}
	ASSERT(mImageInfos.size() == shaders::kiTextureCount);

	for (int64_t i = 0; const common::crc_t& rCrc : data::kpUiTextureCrcs)
	{
		mUiImageInfos.emplace_back(nullptr, mTextureMap.at(rCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		mUiImageInfosMap.try_emplace(rCrc, i++);
	}
	ASSERT(mUiImageInfos.size() == shaders::kiUiTextureCount);

	// Pad mUiImageInfos to kiMaxTextureCount for shader descriptor array compatibility
	// Widgets shader declares: uniform texture2D pTextures[kiMaxTextureCount]
	// Vulkan requires ALL descriptor array elements to be written, even if unused
	VkImageView placeholderImageView = mUiImageInfos[0].imageView;  // Use first UI texture as placeholder
	while (mUiImageInfos.size() < shaders::kiMaxTextureCount)
	{
		mUiImageInfos.emplace_back(nullptr, placeholderImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	// Initialize particle texture pointers
	for (int64_t i = 0; i < kSquareParticleCrcs.miCount; ++i)
	{
		mpSquareParticleTextures[i] = &mTextureMap.at(kSquareParticleCrcs[i]);
	}
	for (int64_t i = 0; i < kLongParticleCrcs.miCount; ++i)
	{
		mpLongParticleTextures[i] = &mTextureMap.at(kLongParticleCrcs[i]);
	}
	for (int64_t i = kLongParticleCrcs.miCount; i < shaders::kiParticlesCookieCount; ++i)
	{
		mpLongParticleTextures[i] = mpLongParticleTextures[shaders::kiLongParticlesCookieCount - 1];
	}

	// Initialize island texture pointers
	mElevationTextures.resize(game::Frame::kiIslandCount);
	mColorTextures.resize(game::Frame::kiIslandCount);
	mNormalsTextures.resize(game::Frame::kiIslandCount);
	mAmbientOcclusionTextures.resize(game::Frame::kiIslandCount);

	// Collect all island texture CRCs for batch loading
	for (int64_t iIndex = 0; const auto& [rCrc, rLazyChunk] : gpFileManager->GetLazyChunkMap())
	{
		if (!(rLazyChunk.header.flags & common::ChunkFlags::kIsland))
		{
			continue;
		}

		if (iIndex >= game::Frame::kiIslandCount)
		{
			DEBUG_BREAK();
			break;
		}

		mElevationTextures[iIndex] = &mTextureMap.at(rLazyChunk.header.islandHeader.elevationCrc);
		smPriorityTextures.push_back(mElevationTextures[iIndex]->mInfo.crc);

		mColorTextures[iIndex] = &mTextureMap.at(rLazyChunk.header.islandHeader.colorsCrc);
		smPriorityTextures.push_back(mColorTextures[iIndex]->mInfo.crc);

		mNormalsTextures[iIndex] = &mTextureMap.at(rLazyChunk.header.islandHeader.normalsCrc);
		smPriorityTextures.push_back(mNormalsTextures[iIndex]->mInfo.crc);

		mAmbientOcclusionTextures[iIndex] = &mTextureMap.at(rLazyChunk.header.islandHeader.ambientOcclusionCrc);
		smPriorityTextures.push_back(mAmbientOcclusionTextures[iIndex]->mInfo.crc);

		++iIndex;
	}

	// Request priority textures
	gpFileManager->RequestChunkLoad(smPriorityTextures, LoadPriority::kRealtime);

	// Request remaining texture at normal priority
	std::vector<common::crc_t> crcs;
	crcs.reserve(mTextureMap.size());
	for (const auto& [rCrc, rTexture] : mTextureMap)
	{
		crcs.push_back(rTexture.mInfo.crc);
	}
	gpFileManager->RequestChunkLoad(crcs);

	BOOT_TIMER_STOP(kBootTimerTextureUpload);

	BOOT_TIMER_START(kGltfTexturesGeneration);

	// Generate or load glTF textures
	GenerateGltfCubemap(true);
	GenerateGltfCubemap(false);
	GenerateGltfLutBrdf();

	BOOT_TIMER_STOP(kGltfTexturesGeneration);
}

TextureManager::~TextureManager()
{
	DestroySamplers();
	DestroyLightingTextures();
	
	gpTextureManager = nullptr;
}

void TextureManager::DestroySamplers()
{
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerSmoke, nullptr);
	mVkSamplerSmoke = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerBorder, nullptr);
	mVkSamplerBorder = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerClamp, nullptr);
	mVkSamplerClamp = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerRepeat, nullptr);
	mVkSamplerRepeat = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerMirroredRepeat, nullptr);
	mVkSamplerMirroredRepeat = VK_NULL_HANDLE;
	vkDestroySampler(gpDeviceManager->mVkDevice, mVkSamplerNearestBorder, nullptr);
	mVkSamplerNearestBorder = VK_NULL_HANDLE;
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
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerSmoke, "Smoke");

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
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerClamp, "Clamp");
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerBorder));
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerBorder, "Border");
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerRepeat));
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerRepeat, "Repeat");
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerMirroredRepeat));
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerMirroredRepeat, "MirroredRepeat");
	vkSamplerCreateInfo.magFilter = VK_FILTER_NEAREST,
	vkSamplerCreateInfo.minFilter = VK_FILTER_NEAREST,
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.anisotropyEnable = VK_FALSE;
	vkSamplerCreateInfo.maxAnisotropy = 0.0f;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerNearestBorder));
	VK_NAME(VK_OBJECT_TYPE_SAMPLER, mVkSamplerNearestBorder, "NearestBorder");
}

void TextureManager::DestroyLightingTextures()
{
	if (mLightingVkFramebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(gpDeviceManager->mVkDevice, mLightingVkFramebuffer, nullptr);
		mLightingVkFramebuffer = VK_NULL_HANDLE;
		vkDestroyRenderPass(gpDeviceManager->mVkDevice, mLightingVkRenderPass, nullptr);
		mLightingVkRenderPass = VK_NULL_HANDLE;
	}
}

void TextureManager::CreateLightingTextures()
{
	DestroyLightingTextures();

	auto [iLightingTextureX, iLightingTextureY] = DetailTextureSize(gLightingTextureMultiplier.Get());
	// Create 3 lighting textures without individual render passes
	TextureInfo lightingTextureInfo
	{
		.textureFlags = {},
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
		.eTextureLayout = kShaderReadOnly,
	};
	mpLightingTextures[0].Create(lightingTextureInfo);
	lightingTextureInfo.pcName = "GreenLighting";
	mpLightingTextures[1].Create(lightingTextureInfo);
	lightingTextureInfo.pcName = "BlueLighting";
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
	VK_NAME(VK_OBJECT_TYPE_RENDER_PASS, mLightingVkRenderPass, "LightingMRT");

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
	VK_NAME(VK_OBJECT_TYPE_FRAMEBUFFER, mLightingVkFramebuffer, "LightingMRT");

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
		.eTextureLayout = kComputeReadWrite,
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
	// Try to load irradiance or pre-filtered cubemap from cache
	VkFormat vkFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	int64_t iSize = bIrradiance ? 64 : 512;
	int64_t iMipCount = static_cast<int64_t>(std::floor(std::log2(iSize))) + 1;

	if (bIrradiance)
	{
		miGltfCubeMipCount = iMipCount;
	}

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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_CUBE,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.eTextureLayout = kShaderReadOnly,
	};

	if (bIrradiance)
	{
		mGltfIrradianceTexture.Create(textureInfo);
		if (TryLoadCachedTexture("IrradianceCubemap.cache", mGltfIrradianceTexture, vkFormat, iSize, iSize, iMipCount, 6))
		{
			return;
		}
	}
	else
	{
		mGltfPreFilteredTexture.Create(textureInfo);
		if (TryLoadCachedTexture("PreFilteredCubemap.cache", mGltfPreFilteredTexture, vkFormat, iSize, iSize, iMipCount, 6))
		{
			return;
		}
	}

	LOG("FormatSupportsColorAttachment? VK_FORMAT_R32G32B32A32_SFLOAT {} VK_FORMAT_R16G16B16A16_SFLOAT {}", FormatSupportsColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT), FormatSupportsColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT));

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

	// Wait for skybox texture to be loaded
	gpTextureManager->WaitForTextures(std::to_array<common::crc_t>({data::kTexturesCRyfjalletCrc}));

	// Transition destination texture to transfer destination layout before copies
	{
		OneShotCommandBuffer oneShotCommandBuffer;
		if (bIrradiance)
		{
			mGltfIrradianceTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kShaderReadOnly, kTransferDestination);
		}
		else
		{
			mGltfPreFilteredTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kShaderReadOnly, kTransferDestination);
		}
		oneShotCommandBuffer.Execute(true);
	}

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
					{.flags = DescriptorFlags::kCombinedSamplers, .iCount = 1, .textureCrc = data::kTexturesCRyfjalletCrc},
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
	if (bIrradiance)
	{
		mGltfIrradianceTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	}
	else
	{
		mGltfPreFilteredTexture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, kTransferDestination, kShaderReadOnly);
	}
	oneShotCommandBuffer.Execute(true);

	// Save generated texture to cache
	SaveTextureToCache(bIrradiance ? "IrradianceCubemap.cache" : "PreFilteredCubemap.cache", bIrradiance ? mGltfIrradianceTexture : mGltfPreFilteredTexture, vkFormat);
}

void TextureManager::ProcessPendingTextures()
{
	for (auto& [rCrc, rTexture] : mTextureMap)
	{
		if (rTexture.mInfo.textureFlags & TextureFlags::kLoaded)
		{
			continue;
		}

		if (gpFileManager->IsChunkReady(rCrc))
		{
			rTexture.mInfo.textureFlags |= TextureFlags::kLoaded;

			rTexture.UpdateData([&](void* pData, int64_t iPosition, int64_t iSize)
			{
				memcpy(pData, &gpFileManager->GetLazyChunkMap().at(rCrc).data[iPosition], iSize);
			});
		}
	}
}

void TextureManager::WaitForTextures(std::span<const common::crc_t> crcs)
{
	// Wait for all chunks to be loaded
	gpFileManager->WaitForChunks(crcs);

	// Update each texture with loaded data
	for (common::crc_t crc : crcs)
	{
		Texture& rTexture = mTextureMap.at(crc);
		if (rTexture.mInfo.textureFlags & TextureFlags::kLoaded)
		{
			continue;
		}

		rTexture.mInfo.textureFlags |= TextureFlags::kLoaded;

		rTexture.UpdateData([&](void* pData, int64_t iPosition, int64_t iSize)
		{
			memcpy(pData, &gpFileManager->GetLazyChunkMap().at(crc).data[iPosition], iSize);
		});
	}
}

void TextureManager::WaitForTextures(std::span<Texture* const> textures)
{
	std::vector<common::crc_t> crcs;
	crcs.reserve(textures.size());
	for (Texture* pTexture : textures)
	{
		crcs.push_back(pTexture->mInfo.crc);
	}

	WaitForTextures(crcs);
}

void TextureManager::GenerateGltfLutBrdf()
{
	// Try to load BRDF LUT from cache
	VkFormat vkFormat = VK_FORMAT_R16G16_SFLOAT;
	int64_t iSize = 512;

	if (gpFileManager->Exists({FileFlags::kAppDataDirectory}, "BrdfLut.cache"))
	{
		// Create texture optimized for loading from cache
		TextureInfo textureInfo
		{
			.textureFlags = {},
			.pcName = "GltfLutBrdf",
			.flags = {},
			.format = vkFormat,
			.extent = VkExtent3D {static_cast<uint32_t>(iSize), static_cast<uint32_t>(iSize), 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.eTextureLayout = kShaderReadOnly,
		};
		mGltfLutBrdfTexture.Create(textureInfo);

		if (TryLoadCachedTexture("BrdfLut.cache", mGltfLutBrdfTexture, vkFormat, iSize, iSize, 1, 1))
		{
			return;
		}
	}

	// Create texture with render pass support for generation
	mGltfLutBrdfTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.pcName = "LutBrdf",
		.flags = 0,
		.format = vkFormat,
		.extent = VkExtent3D {512, 512, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

	// Save generated texture to cache
	SaveTextureToCache("BrdfLut.cache", mGltfLutBrdfTexture, vkFormat);
}

bool TextureManager::TryLoadCachedTexture(const std::filesystem::path& rCachePath, Texture& rTexture, VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers)
{
	if (!gpFileManager->Exists({FileFlags::kAppDataDirectory}, rCachePath))
	{
		return false;
	}

	std::fstream fileStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, rCachePath);
	if (!fileStream.is_open())
	{
		return false;
	}

	TextureFileCacheHeader header {};
	fileStream.read(reinterpret_cast<char*>(&header), sizeof(TextureFileCacheHeader));

	// Validate header
	if (header.iMagic != TextureFileCacheHeader::kiMagic || header.iVersion != TextureFileCacheHeader::kiVersion || header.vkFormat != vkFormat || header.iWidth != iWidth || header.iHeight != iHeight || header.iMipLevels != iMipLevels || header.iArrayLayers != iArrayLayers)
	{
		fileStream.close();
		LOG("Invalid cache file {}, regenerating", rCachePath.string());
		return false;
	}

	// Read texture data
	std::vector<std::byte> data(header.iDataSize);
	fileStream.read(reinterpret_cast<char*>(data.data()), header.iDataSize);
	fileStream.close();

	// Update texture with cached data
	rTexture.UpdateData([&data](void* pData, [[maybe_unused]] int64_t iPosition, int64_t iSize)
	{
		memcpy(pData, data.data(), iSize);
	});

	LOG("Loaded cached texture from {}", rCachePath.string());
	return true;
}

// Save texture to cache file
void TextureManager::SaveTextureToCache(const std::filesystem::path& rCachePath, const Texture& rTexture, VkFormat vkFormat)
{
	// Prepare header
	TextureFileCacheHeader header {};
	header.iMagic = TextureFileCacheHeader::kiMagic;
	header.iVersion = TextureFileCacheHeader::kiVersion;
	header.vkFormat = vkFormat;
	header.iWidth = rTexture.mInfo.extent.width;
	header.iHeight = rTexture.mInfo.extent.height;
	header.iMipLevels = rTexture.mInfo.mipLevels;
	header.iArrayLayers = rTexture.mInfo.arrayLayers;

	// Read texture data from GPU
	std::vector<std::byte> data;
	CopyImageToHostMemory(rTexture.mVkImage, rTexture.mInfo.extent, vkFormat, rTexture.mInfo.mipLevels, rTexture.mInfo.arrayLayers, false, data);

	header.iDataSize = static_cast<int64_t>(data.size());

	// Write cache file
	gpFileManager->RemoveFile({FileFlags::kAppDataDirectory}, rCachePath);
	std::fstream fileStreamOut = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, rCachePath);
	common::Write(fileStreamOut, header);
	common::Write(fileStreamOut, data);
	fileStreamOut.flush();
	fileStreamOut.close();

	LOG("Saved texture cache to {}", rCachePath.string());
}

} // namespace engine
