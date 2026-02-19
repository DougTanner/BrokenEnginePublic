#include "TextureManager.h"

#include "BufferManager.h"
#include "DeviceManager.h"
#include "InstanceManager.h"
#include "ShaderManager.h"
#include "SwapchainManager.h"
#include "TextureUploadManager.h"
#include "ThreadLocal.h"
#include "File/FileManager.h"
#include "Frame/Render.h"
#include "Graphics/Graphics.h"
#include "Graphics/GraphicsUtils.h"
#include "Graphics/Islands.h"
#include "Graphics/OneShotCommandBuffer.h"
#include "Profile/ProfileManager.h"

#include "Frame/Frame.h"

#include "Data/Data.h"

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

	// Use VMA's pre-mapped pointer to copy data to output (VMA guarantees pMappedData valid for mapped allocations)
#pragma warning(suppress: 6387)
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

	ScopedBootTimer scopedBootTimer(kBootTimerTextureManager);

	CreateSamplers();

	CreateLightingTextures();
	CreateShadowTextures();
	CreateSmokeTextures();
	CreateWindTextures();
	CreateObjectShadowsTextures();

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

	auto [iTerrainElevationTextureX, iTerrainElevationTextureY] = DetailTextureSize(gTerrainElevationTextureMultiplier.Get());
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
		.renderPassVkClearColorValue = {gpIslands->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainColorTextureX, iTerrainColorTextureY] = DetailTextureSize(gTerrainColorTextureMultiplier.Get());
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

	auto [iTerrainNormalTextureX, iTerrainNormalTextureY] = DetailTextureSize(gTerrainNormalTextureMultiplier.Get());
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

	auto [iTerrainAmbientOcclusionTextureX, iTerrainAmbientOcclusionTextureY] = DetailTextureSize(gTerrainAmbientOcclusionTextureMultiplier.Get());
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
	mImageInfos.resize(mTextureMap.size(), {nullptr, mWhiteTexture.mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});

	CreateGlobalDescriptorSet();

	// Initialize island texture pointers
	mElevationTextures.resize(game::Frame::kiIslandCount);
	mColorTextures.resize(game::Frame::kiIslandCount);
	mNormalsTextures.resize(game::Frame::kiIslandCount);
	mAmbientOcclusionTextures.resize(game::Frame::kiIslandCount);

	// Reset to initial priority textures (remove island CRCs appended by previous construction)
	static const size_t kuiInitialPriorityTextureCount = smPriorityTextures.size();
	smPriorityTextures.resize(kuiInitialPriorityTextureCount);

	// Collect all island texture CRCs for batch loading
	for (int64_t iIndex = 0; common::crc_t islandCrc : gpIslands->smPriorityIslands)
	{
		if (iIndex >= game::Frame::kiIslandCount)
		{
			DEBUG_BREAK();
			break;
		}

		const LazyChunk& rLazyChunk = gpFileManager->GetLazyChunk(islandCrc);

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
	miPbrCubeMipCount = mTextureMap.at(kPrefilteredCrc).mInfo.mipLevels;
	GeneratePbrLutBrdf();

	gpProfileManager->BootStop(kModelTexturesGeneration);

	// Request priority textures
	gpFileManager->RequestChunkLoad(smPriorityTextures, LoadPriority::kRealtime);
}

TextureManager::~TextureManager()
{
	if (mGlobalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, gpDeviceManager->mVkDescriptorPool, static_cast<uint32_t>(mGlobalDescriptorSets.size()), mGlobalDescriptorSets.data());
		vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mGlobalDescriptorSetLayout, nullptr);
	}

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mAcquireVkCommandPool, nullptr);

	DestroySamplers();
	DestroyLightingTextures();

	gpTextureManager = nullptr;
}

void TextureManager::DestroyScreenDependentResources()
{
	if (mGlobalDescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkFreeDescriptorSets(gpDeviceManager->mVkDevice, gpDeviceManager->mVkDescriptorPool, static_cast<uint32_t>(mGlobalDescriptorSets.size()), mGlobalDescriptorSets.data());
		vkDestroyDescriptorSetLayout(gpDeviceManager->mVkDevice, mGlobalDescriptorSetLayout, nullptr);
		mGlobalDescriptorSetLayout = VK_NULL_HANDLE;
		mGlobalDescriptorSets.clear();
	}

	vkDestroyCommandPool(gpDeviceManager->mVkDevice, mAcquireVkCommandPool, nullptr);
	mAcquireVkCommandPool = VK_NULL_HANDLE;
	mAcquireVkCommandBuffers.clear();

	DestroyLightingTextures();
}

void TextureManager::CreateScreenDependentResources()
{
	CreateLightingTextures();
	CreateShadowTextures();
	CreateSmokeTextures();
	CreateWindTextures();
	CreateObjectShadowsTextures();

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

	auto [iTerrainElevationTextureX, iTerrainElevationTextureY] = DetailTextureSize(gTerrainElevationTextureMultiplier.Get());
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
		.renderPassVkClearColorValue = {gpIslands->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
		.eTextureLayout = kShaderReadOnly,
	});

	auto [iTerrainColorTextureX, iTerrainColorTextureY] = DetailTextureSize(gTerrainColorTextureMultiplier.Get());
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

	auto [iTerrainNormalTextureX, iTerrainNormalTextureY] = DetailTextureSize(gTerrainNormalTextureMultiplier.Get());
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

	auto [iTerrainAmbientOcclusionTextureX, iTerrainAmbientOcclusionTextureY] = DetailTextureSize(gTerrainAmbientOcclusionTextureMultiplier.Get());
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

	CreateGlobalDescriptorSet();

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
	vkSamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
	vkSamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
	vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	vkSamplerCreateInfo.anisotropyEnable = VK_FALSE;
	vkSamplerCreateInfo.maxAnisotropy = 0.0f;
	CHECK_VK(vkCreateSampler(gpDeviceManager->mVkDevice, &vkSamplerCreateInfo, nullptr, &mVkSamplerNearestBorder));
	VkName(VK_OBJECT_TYPE_SAMPLER, mVkSamplerNearestBorder, "NearestBorder");
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

void TextureManager::CreateShadowTextures()
{
	auto [iShadowTextureX, iShadowTextureY] = DetailTextureSize(gWorldDetail.Get());
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
		.renderPassVkClearColorValue = {gpIslands->mfSeaFloorElevation, 0.0f, 0.0f, 1.0f},
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

void TextureManager::CreateSmokeTextures()
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

void TextureManager::CreateWindTextures()
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

void TextureManager::CreateObjectShadowsTextures()
{
	auto [iObjectShadowsRenderTextureX, iObjectShadowsRenderTextureY] = DetailTextureSize(gObjectShadowsRenderMultiplier.Get());
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

	auto [iObjectShadowsBlurTextureX, iObjectShadowsBlurTextureY] = DetailTextureSize(gObjectShadowsBlurMultiplier.Get());
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
	else if (flags & DescriptorFlags::kSamplerWindClamp)
	{
		return mVkSamplerWindClamp;
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

			if (bNeedAcquireBarrier && bFromTransferQueue)
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
			UpdateDescriptorsForTexture(rCrc);
			bAdoptedTextures = true;

			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);

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
			UpdateDescriptorsForTexture(rCrc);
			bAdoptedTextures = true;

			rLazyChunk.eState.store(ChunkState::kReady, std::memory_order_release);

			// Only upload one texture a frame
			break;
		}
	}

	// Flush deferred texture array descriptor writes
	if (bAdoptedTextures)
	{
		UpdateTextureArrayDescriptors();
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
	common::gpThreadLocal->mWorkbuffer.Push();
	for (Texture* pTexture : textures)
	{
		common::gpThreadLocal->mWorkbuffer.PushBack<common::crc_t>(pTexture->mInfo.crc);
	}

	WaitForTextures(common::gpThreadLocal->mWorkbuffer.Span<common::crc_t>());
	common::gpThreadLocal->mWorkbuffer.Pop();
}

void TextureManager::GeneratePbrLutBrdf()
{
	if constexpr (kbRandomlyInvalidatePbrCubemapCache)
	{
		common::RandomEngine randomEngine(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (common::Random(10, randomEngine) == 0)
		{
			Log("Randomly invalidating GLTF BRDF LUT cache");
			gpFileManager->RemoveFile({FileFlags::kAppDataDirectory}, "BrdfLut.cache");
		}
	}

	// Try to load BRDF LUT from cache
	VkFormat vkFormat = VK_FORMAT_R16G16_SFLOAT;
	int64_t iSize = 512;

	if (gpFileManager->Exists({FileFlags::kAppDataDirectory}, "BrdfLut.cache"))
	{
		// Create texture optimized for loading from cache
		TextureInfo textureInfo
		{
			.textureFlags = {},
			.name = "PbrLutBrdf",
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
		mPbrLutBrdfTexture.Create(textureInfo);

		if (TryLoadCachedTexture("BrdfLut.cache", mPbrLutBrdfTexture, vkFormat, iSize, iSize, 1, 1))
		{
			return;
		}
	}

	// Create texture with render pass support for generation
	mPbrLutBrdfTexture.Create(
	{
		.textureFlags = {kRenderPass},
		.name = "LutBrdf",
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
		.name = "PbrCubemap",
		.flags = {PipelineFlags::kRenderTarget},
		.ppShaders = {&gpShaderManager->mShaders.at(data::kShadersModelModelGenBrdfLutvertCrc), &gpShaderManager->mShaders.at(data::kShadersModelModelGenBrdfLutfragCrc)},
		.pVertexBuffer = &gpBufferManager->mQuadsVertexBuffer,
		.vkRenderPass = mPbrLutBrdfTexture.mVkRenderPass,
		.vkExtent3D = mPbrLutBrdfTexture.mInfo.extent,
		.pDescriptorInfos =
		{
		},
	});

	OneShotCommandBuffer oneShotCommandBuffer;

	mPbrLutBrdfTexture.RecordBeginRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
	pipeline.RecordDraw(0, oneShotCommandBuffer.mVkCommandBuffer, 1, 0);
	mPbrLutBrdfTexture.RecordEndRenderPass(oneShotCommandBuffer.mVkCommandBuffer);

	oneShotCommandBuffer.Execute(true);

	// Save generated texture to cache
	SaveTextureToCache("BrdfLut.cache", mPbrLutBrdfTexture, vkFormat);
}

bool TextureManager::TryLoadCachedTexture(const std::filesystem::path& rCachePath, Texture& rTexture, VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers, common::crc_t sourceCrc)
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

	// Validate header (including source CRC if provided)
	if (header.iMagic != TextureFileCacheHeader::kiMagic || header.iVersion != TextureFileCacheHeader::kiVersion || header.vkFormat != vkFormat || header.iWidth != iWidth || header.iHeight != iHeight || header.iMipLevels != iMipLevels || header.iArrayLayers != iArrayLayers || (sourceCrc != 0 && header.sourceCrc != sourceCrc))
	{
		fileStream.close();
		Log("Invalid cache file {} (sourceCrc mismatch: cached={:#x} expected={:#x}), regenerating", rCachePath.string(), header.sourceCrc, sourceCrc);
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

	Log("Loaded cached texture from {}", rCachePath.string());
	return true;
}

// Save texture to cache file
void TextureManager::SaveTextureToCache(const std::filesystem::path& rCachePath, const Texture& rTexture, VkFormat vkFormat, common::crc_t sourceCrc)
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
	header.sourceCrc = sourceCrc;

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

	Log("Saved texture cache to {}", rCachePath.string());
}

void TextureManager::RegisterTextureBinding(common::crc_t crc, Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags, Texture* pTexture, Texture** ppTextures, int64_t iCount)
{
	std::vector<Texture*> textures;
	if (ppTextures != nullptr)
	{
		textures.assign(ppTextures, ppTextures + iCount);
	}
	mTextureBindings[crc].push_back({pPipeline, iBinding, samplerFlags, pTexture, std::move(textures)});
}

void TextureManager::RegisterStandaloneSamplerBinding(Pipeline* pPipeline, int64_t iBinding, DescriptorFlags_t samplerFlags)
{
	mStandaloneSamplerBindings.push_back({pPipeline, iBinding, samplerFlags});
}

void TextureManager::UpdateDescriptorsForTexture(common::crc_t crc)
{
	VkImageView vkImageView = mTextureMap.at(crc).mVkImageView;

	// Update individual combined image sampler bindings
	auto it = mTextureBindings.find(crc);
	if (it != mTextureBindings.end())
	{
		for (const TextureBinding& rBinding : it->second)
		{
			VkSampler vkSampler = GetSampler(rBinding.samplerFlags);
			if (!rBinding.textures.empty())
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else
			{
				rBinding.pPipeline->UpdateCombinedImageSamplerDescriptor(rBinding.iBinding, vkImageView, vkSampler);
			}
		}
	}

	// Ensure CRC has an assigned index and store updated imageView
	mImageInfos.at(static_cast<int64_t>(CrcToIndex(crc))).imageView = vkImageView;
}

void TextureManager::CreateGlobalDescriptorSet()
{
	// Global Set 0 layout:
	//   Binding 0:  globalUniform (UNIFORM_BUFFER, ALL_GRAPHICS)
	//   Binding 1:  mainUniform   (UNIFORM_BUFFER, ALL_GRAPHICS)
	//   Binding 3:  samplerRepeat (SAMPLER, FRAGMENT)
	//   Binding 4:  pTextures[]   (SAMPLED_IMAGE, FRAGMENT, PARTIALLY_BOUND | UPDATE_AFTER_BIND)
	//   Binding 12: samplerClamp  (SAMPLER, FRAGMENT)
	VkDescriptorSetLayoutBinding pBindings[]
	{
		{.binding = 0,  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 1,  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 3,  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 4,  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
		{.binding = 12, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
	};

	VkDescriptorBindingFlags pBindingFlags[]
	{
		0,
		0,
		0,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		0,
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.pNext = nullptr,
		.bindingCount = static_cast<uint32_t>(std::size(pBindingFlags)),
		.pBindingFlags = pBindingFlags,
	};

	VkDescriptorSetLayoutCreateInfo layoutCreateInfo
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &bindingFlagsCreateInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = static_cast<uint32_t>(std::size(pBindings)),
		.pBindings = pBindings,
	};

	CHECK_VK(vkCreateDescriptorSetLayout(gpDeviceManager->mVkDevice, &layoutCreateInfo, nullptr, &mGlobalDescriptorSetLayout));
	VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, mGlobalDescriptorSetLayout, "GlobalSet0");

	int64_t iFramebufferCount = static_cast<int64_t>(gpSwapchainManager->mFramebuffers.size());
	mGlobalDescriptorSets.resize(iFramebufferCount);
	for (int64_t i = 0; i < iFramebufferCount; ++i)
	{
		VkDescriptorSetAllocateInfo allocInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.pNext = nullptr,
			.descriptorPool = gpDeviceManager->mVkDescriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &mGlobalDescriptorSetLayout,
		};
		CHECK_VK(vkAllocateDescriptorSets(gpDeviceManager->mVkDevice, &allocInfo, &mGlobalDescriptorSets[i]));
		VkName(VK_OBJECT_TYPE_DESCRIPTOR_SET, mGlobalDescriptorSets[i], std::format("GlobalSet0{}", i).c_str());
	}

	WriteGlobalDescriptorSets();
}

void TextureManager::WriteGlobalDescriptorSets()
{
	for (int64_t i = 0; i < static_cast<int64_t>(mGlobalDescriptorSets.size()); ++i)
	{
		VkDescriptorBufferInfo globalBufferInfo {.buffer = gpBufferManager->mGlobalLayoutUniformBuffers[i].GetBuffer(), .offset = 0, .range = VK_WHOLE_SIZE};
		VkDescriptorBufferInfo mainBufferInfo {.buffer = gpBufferManager->mMainLayoutUniformBuffers[i].GetBuffer(), .offset = 0, .range = VK_WHOLE_SIZE};
		VkDescriptorImageInfo samplerRepeatInfo {.sampler = mVkSamplerRepeat};
		VkDescriptorImageInfo samplerClampInfo {.sampler = mVkSamplerClamp};

		VkWriteDescriptorSet pWrites[]
		{
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets[i], .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &globalBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets[i], .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &mainBufferInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets[i], .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerRepeatInfo},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets[i], .dstBinding = 4, .descriptorCount = static_cast<uint32_t>(mImageInfos.size()), .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = mImageInfos.data()},
			{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = mGlobalDescriptorSets[i], .dstBinding = 12, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &samplerClampInfo},
		};

		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, static_cast<uint32_t>(std::size(pWrites)), pWrites, 0, nullptr);
	}
}

void TextureManager::UpdateTextureArrayDescriptors()
{
	// Update global Set 0 binding 4 (bindless texture array)
	for (VkDescriptorSet& rVkDescriptorSet : mGlobalDescriptorSets)
	{
		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = 4,
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(mImageInfos.size()),
			.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
			.pImageInfo = mImageInfos.data(),
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};
		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
}

void TextureManager::WriteArrayBindingDescriptors(const TextureBinding& rBinding, VkSampler vkSampler)
{
	int64_t iTextureCount = static_cast<int64_t>(rBinding.textures.size());
	auto* pImageInfos = common::gpThreadLocal->mWorkbuffer.PushBuffer<VkDescriptorImageInfo*>(iTextureCount * static_cast<int64_t>(sizeof(VkDescriptorImageInfo)));
	for (int64_t i = 0; i < iTextureCount; ++i)
	{
		pImageInfos[i].sampler = vkSampler;
		pImageInfos[i].imageView = rBinding.textures.at(i) != nullptr ? rBinding.textures.at(i)->mVkImageView : mWhiteTexture.mVkImageView;
		pImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	for (VkDescriptorSet& rVkDescriptorSet : rBinding.pPipeline->mVkDescriptorSets)
	{
		VkWriteDescriptorSet vkWriteDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = nullptr,
			.dstSet = rVkDescriptorSet,
			.dstBinding = static_cast<uint32_t>(rBinding.iBinding),
			.dstArrayElement = 0,
			.descriptorCount = static_cast<uint32_t>(iTextureCount),
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = pImageInfos,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr,
		};
		vkUpdateDescriptorSets(gpDeviceManager->mVkDevice, 1, &vkWriteDescriptorSet, 0, nullptr);
	}
	common::gpThreadLocal->mWorkbuffer.Pop();
}

void TextureManager::RewriteSamplerDescriptors()
{
	// Update standalone sampler descriptors in per-pipeline sets
	for (const StandaloneSamplerBinding& rBinding : mStandaloneSamplerBindings)
	{
		VkSampler vkSampler = GetSampler(rBinding.samplerFlags);
		rBinding.pPipeline->UpdateSamplerDescriptor(rBinding.iBinding, vkSampler);
	}

	// Update combined image sampler descriptors in per-pipeline sets
	for (auto& [rCrc, rBindings] : mTextureBindings)
	{
		for (const TextureBinding& rBinding : rBindings)
		{
			VkSampler vkSampler = GetSampler(rBinding.samplerFlags);

			if (!rBinding.textures.empty())
			{
				WriteArrayBindingDescriptors(rBinding, vkSampler);
			}
			else
			{
				VkImageView vkImageView = VK_NULL_HANDLE;
				if (rBinding.pTexture != nullptr)
				{
					vkImageView = rBinding.pTexture->mVkImageView;
				}
				else
				{
					auto it = mTextureMap.find(rCrc);
					if (it != mTextureMap.end())
					{
						vkImageView = it->second.mVkImageView;
					}
				}
				if (vkImageView != VK_NULL_HANDLE)
				{
					rBinding.pPipeline->UpdateCombinedImageSamplerDescriptor(rBinding.iBinding, vkImageView, vkSampler);
				}
			}
		}
	}
}

void TextureManager::ClearTextureBindings()
{
	mTextureBindings.clear();
	mStandaloneSamplerBindings.clear();
}

float TextureManager::CrcToIndex(common::crc_t crc)
{
	auto it = mImageInfosMap.find(crc);
	if (it != mImageInfosMap.end())
	{
		return static_cast<float>(it->second);
	}

	// Heap: unordered_map emplace may allocate. Entries map CRC->index permanently for the texture array,
	//   so a workbuffer (frame-scoped) can't own them, and we can't pre-populate without knowing all CRCs
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	int64_t iIndex = mNextTextureIndex++;
	ASSERT(iIndex < static_cast<int64_t>(mImageInfos.size()));
	mImageInfosMap.emplace(crc, iIndex);
	return static_cast<float>(iIndex);
}

} // namespace engine
