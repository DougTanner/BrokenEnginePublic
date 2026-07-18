#if defined(BT_CLIENT)

#include "TextureCache.h"

#include "Data/Data.h"

namespace engine
{

void TextureCache::CopyImageToHostMemory(VkImage srcImage, VkExtent3D extent, VkFormat format, uint32_t mipLevels, uint32_t arrayLayers, bool bFromSwapchain, VkImageLayout vkCurrentLayout, std::vector<std::byte>& rOutData)
{
	// Heap: rOutData.resize + staging-buffer creation. Main-loop-reachable per frame via the kbScreenshots trigger
	// (Graphics::RenderMainPresentAcquire -> Screenshot::SaveScreenshot -> here) with tracking live; rOutData
	// is std::move'd into the async save lambda so it cannot use the workbuffer.
	ScopedSuppressAllocationTracking suppress;

	VkPipelineStageFlags srcStage = bFromSwapchain ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkPipelineStageFlags dstStage = bFromSwapchain ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	VkAccessFlags srcAccess = bFromSwapchain ? 0 : VK_ACCESS_SHADER_READ_BIT;
	VkAccessFlags dstAccess = bFromSwapchain ? 0 : VK_ACCESS_SHADER_READ_BIT;

	// Calculate total data size
	int64_t iTotalSize = common::ComputeImageByteSize(format, extent.width, extent.height, mipLevels, arrayLayers, 1);

	// Allocate output data
	rOutData.resize(iTotalSize);

	// Create staging buffer
	StagingBuffer stagingBuffer("ImageCopyStaging", iTotalSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

	OneShotCommandBuffer oneShotCommandBuffer;

	// Transition image to transfer source layout
	VkImageMemoryBarrier vkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = srcAccess,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = vkCurrentLayout,
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
	size_t uiOffset = 0;

	for (uint32_t iLayer = 0; iLayer < arrayLayers; ++iLayer)
	{
		int64_t iMipWidth = extent.width;
		int64_t iMipHeight = extent.height;
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

			vkCmdCopyImageToBuffer(oneShotCommandBuffer.mVkCommandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer.vkBuffer, 1, &vkBufferImageCopy);

			uiOffset += common::SizeInBytes(format, iMipWidth, iMipHeight);
			iMipWidth = std::max(iMipWidth / 2, 1ll);
			iMipHeight = std::max(iMipHeight / 2, 1ll);
		}
	}

	// Transition image back to original layout
	vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	vkImageMemoryBarrier.newLayout = vkCurrentLayout;
	vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkImageMemoryBarrier.dstAccessMask = dstAccess;
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage, 0, 0, nullptr, 0, nullptr, 1, &vkImageMemoryBarrier);

	oneShotCommandBuffer.Execute();

	// Use VMA's pre-mapped pointer to copy data to output (VMA guarantees pMappedData valid for mapped allocations)
#pragma warning(suppress: 6387)
	std::memcpy(rOutData.data(), stagingBuffer.vmaAllocationInfo.pMappedData, iTotalSize);
}

void TextureCache::GeneratePbrLutBrdf()
{
	if constexpr (kbRandomlyInvalidatePbrCubemapCache)
	{
		common::RandomEngine randomEngine(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		if (common::Random(10u, randomEngine) == 0)
		{
			LOG(kGraphics, kDebug, "Randomly invalidating GLTF BRDF LUT cache");
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

		if (TryLoadCachedTexture("BrdfLut.cache", mPbrLutBrdfTexture))
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

	// Scoped so the render OneShotCommandBuffer is destroyed (releasing sbInUse) before SaveTextureToCache's
	// CopyImageToHostMemory constructs its own — overlapping lifetimes trip the shared-pool assert.
	{
		OneShotCommandBuffer oneShotCommandBuffer;

		mPbrLutBrdfTexture.RecordBeginRenderPass(oneShotCommandBuffer.mVkCommandBuffer);
		pipeline.RecordDraw(0, oneShotCommandBuffer.mVkCommandBuffer, 1, 0);
		mPbrLutBrdfTexture.RecordEndRenderPass(oneShotCommandBuffer.mVkCommandBuffer);

		oneShotCommandBuffer.Execute();
	}

	// Save generated texture to cache
	SaveTextureToCache("BrdfLut.cache", mPbrLutBrdfTexture);
}

bool TextureCache::TryLoadCachedTexture(const std::filesystem::path& rCachePath, Texture& rTexture, common::crc_t sourceCrc)
{
	// Heap: the cached-payload vector below runs from GeneratePbrLutBrdf in the PipelineManager ctor, which also fires on pipeline-tier recreate (settings change / device loss) with the main-loop tracker armed. Mirrors SaveTextureToCache's CopyImageToHostMemory suppression.
	ScopedSuppressAllocationTracking suppress;

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
	if (!fileStream || header.iMagic != TextureFileCacheHeader::kiMagic || header.iVersion != TextureFileCacheHeader::kiVersion || header.vkFormat != rTexture.mInfo.format || header.iWidth != rTexture.mInfo.extent.width || header.iHeight != rTexture.mInfo.extent.height || header.iMipLevels != rTexture.mInfo.mipLevels || header.iArrayLayers != rTexture.mInfo.arrayLayers || (sourceCrc != 0 && header.sourceCrc != sourceCrc))
	{
		fileStream.close();
		LOG(kGraphics, kWarning, "Invalid cache file {} (header validation failed), regenerating", rCachePath.string());
		return false;
	}

	// The on-disk iDataSize is opaque (cache file is a trust boundary); validate it against the size computed from the already-validated dims/format before trusting it. A too-small value would overread in the upload memcpy below; a negative value would blow up the std::vector ctor.
	const int64_t iExpectedDataSize = common::ComputeImageByteSize(rTexture.mInfo.format, rTexture.mInfo.extent.width, rTexture.mInfo.extent.height, rTexture.mInfo.mipLevels, rTexture.mInfo.arrayLayers, 1);
	if (header.iDataSize != iExpectedDataSize)
	{
		fileStream.close();
		LOG(kGraphics, kWarning, "Invalid cache file {} (iDataSize {} != expected {}), regenerating", rCachePath.string(), header.iDataSize, iExpectedDataSize);
		return false;
	}

	// Read texture data
	std::vector<std::byte> data(header.iDataSize);
	fileStream.read(reinterpret_cast<char*>(data.data()), header.iDataSize);
	if (!fileStream)
	{
		fileStream.close();
		LOG(kGraphics, kWarning, "Invalid cache file {} (truncated payload), regenerating", rCachePath.string());
		return false;
	}
	fileStream.close();

	// Update texture with cached data. Copy data.size() (== the validated iDataSize) rather than the staging size iSize so the read can never exceed the buffer we own (the two are equal for the depth-1 textures the cache holds).
	rTexture.UpdateData([&data](void* pData, [[maybe_unused]] int64_t iPosition, [[maybe_unused]] int64_t iSize)
	{
		std::memcpy(pData, data.data(), data.size());
	});

	LOG(kLoading, kDebug, "Loaded cached texture from {}", rCachePath.string());
	return true;
}

void TextureCache::SaveTextureToCache(const std::filesystem::path& rCachePath, const Texture& rTexture, common::crc_t sourceCrc)
{
	// Prepare header
	TextureFileCacheHeader header {};
	header.iMagic = TextureFileCacheHeader::kiMagic;
	header.iVersion = TextureFileCacheHeader::kiVersion;
	header.vkFormat = rTexture.mInfo.format;
	header.iWidth = rTexture.mInfo.extent.width;
	header.iHeight = rTexture.mInfo.extent.height;
	header.iMipLevels = rTexture.mInfo.mipLevels;
	header.iArrayLayers = rTexture.mInfo.arrayLayers;
	header.sourceCrc = sourceCrc;

	// Read texture data from GPU
	std::vector<std::byte> data;
	CopyImageToHostMemory(rTexture.mVkImage, rTexture.mInfo.extent, rTexture.mInfo.format, rTexture.mInfo.mipLevels, rTexture.mInfo.arrayLayers, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, data);

	header.iDataSize = static_cast<int64_t>(data.size());

	// Write cache file
	static_cast<void>(gpFileManager->WriteFileAtomically({FileFlags::kAppDataDirectory, FileFlags::kWrite}, rCachePath, [&](std::fstream& rStream)
	{
		common::Write(rStream, header);
		common::Write(rStream, data);
	}));

	LOG(kGraphics, kDebug, "Saved texture cache to {}", rCachePath.string());
}

} // namespace engine

#endif // BT_CLIENT
