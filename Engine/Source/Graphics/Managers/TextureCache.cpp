#if defined(BT_CLIENT)

#include "TextureCache.h"

#include "TextureManager.h"

#include "Data/Data.h"

namespace engine
{

void TextureCache::CopyImageToHostMemory(VkImage srcImage, VkExtent3D extent, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool bFromSwapchain, std::vector<std::byte>& rOutData)
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
	rOutData.resize(iTotalSize);

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
	memcpy(rOutData.data(), stagingVmaAllocationInfo.pMappedData, iTotalSize);

	// Cleanup staging buffer
	vmaDestroyBuffer(gpDeviceManager->mpAllocator, stagingVkBuffer, stagingVmaAllocation);
}

void TextureCache::GeneratePbrLutBrdf()
{
	if constexpr (kbRandomlyInvalidatePbrCubemapCache)
	{
		common::RandomEngine randomEngine(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (common::Random(10u, randomEngine) == 0)
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
			.eTextureLayout = TextureLayout::kShaderReadOnly,
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
		.textureFlags = {TextureFlags::kRenderPass},
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
		.eTextureLayout = TextureLayout::kColorAttachment,
	});

	Pipeline pipeline(
	{
		.name = "PbrCubemap",
		.flags = {PipelineFlags::kRenderTarget},
		.ppShaders = {&gpPipelineManager->mShaders.at(data::kShadersModelModelGenBrdfLutvertCrc), &gpPipelineManager->mShaders.at(data::kShadersModelModelGenBrdfLutfragCrc)},
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

bool TextureCache::TryLoadCachedTexture(const std::filesystem::path& rCachePath, Texture& rTexture, VkFormat vkFormat, int64_t iWidth, int64_t iHeight, int64_t iMipLevels, int64_t iArrayLayers, common::crc_t sourceCrc)
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

	Log(kLogLoading, "Loaded cached texture from {}", rCachePath.string());
	return true;
}

void TextureCache::SaveTextureToCache(const std::filesystem::path& rCachePath, const Texture& rTexture, VkFormat vkFormat, common::crc_t sourceCrc)
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

} // namespace engine

#endif // BT_CLIENT
