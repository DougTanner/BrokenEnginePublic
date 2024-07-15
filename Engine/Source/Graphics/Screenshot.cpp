#if defined(ENABLE_SCREENSHOTS)

// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Screenshot.h"

#include "Graphics/Graphics.h"
#include "Graphics/OneShotCommandBuffer.h"

#pragma warning(push, 0)
#pragma warning(disable : 4146 4702 4706 6001 6011 6262 6308 6330 6385 6386 6387 26051 26408 26409 26429 26432 26433 26434 26435 26438 26440 26443 26444 26447 26448 26451 26455 26456 26459 26460 26461 26466 26472 26475 26477 26481 26482 26485 26488 26498 26490 26493 26494 26495 26496 26497 26812 26814 26818 26819 28182 28020)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../ThirdParty/stb/stb_image_write.h"
#pragma warning(pop)

namespace engine
{

void SaveScreenshot()
{
	vkDeviceWaitIdle(gpDeviceManager->mVkDevice);

	VkExtent3D vkExtent3D {static_cast<uint32_t>(gpGraphics->mFramebufferExtent2D.width), static_cast<uint32_t>(gpGraphics->mFramebufferExtent2D.height), 1};

	Texture texture(
	{
		.textureFlags = {TextureFlags::kHostVisible},
		.pcName = "Screenshot",
		.flags = 0,
		.format = gpInstanceManager->mFramebufferVkFormat,
		.extent = vkExtent3D,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.renderPassVkAttachmentLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.eTextureLayout = TextureLayout::kTransferDestination,
	});

	OneShotCommandBuffer oneShotCommandBuffer;

	VkImageMemoryBarrier swapchainVkImageMemoryBarrier
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.pNext = nullptr,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = gpSwapchainManager->mFramebuffers.at(gpSwapchainManager->miFramebufferIndex).presentVkImage,
		.subresourceRange =
		{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainVkImageMemoryBarrier);

	VkImageCopy vkImageCopy
	{
		.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.extent = vkExtent3D,
	};
	vkCmdCopyImage(oneShotCommandBuffer.mVkCommandBuffer, gpSwapchainManager->mFramebuffers.at(gpSwapchainManager->miFramebufferIndex).presentVkImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.mVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vkImageCopy);

	swapchainVkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	swapchainVkImageMemoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
	swapchainVkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	swapchainVkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(oneShotCommandBuffer.mVkCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapchainVkImageMemoryBarrier);

	texture.TransitionImageLayout(oneShotCommandBuffer.mVkCommandBuffer, TextureLayout::kTransferDestination, TextureLayout::kGeneral);

	oneShotCommandBuffer.Execute(true);

	const uint32_t* puiArgb = nullptr;
	CHECK_VK(vkMapMemory(gpDeviceManager->mVkDevice, texture.mVkDeviceMemory, 0, VK_WHOLE_SIZE, 0, (void**)&puiArgb));
	std::vector<uint32_t> rgba(vkExtent3D.width * vkExtent3D.height);
	uint32_t* puiAbgr = rgba.data();
	for (int64_t y = 0; y < vkExtent3D.height; ++y)
	{
		for (int64_t x = 0; x < vkExtent3D.width; ++x)
		{
			uint32_t argb = puiArgb[y * vkExtent3D.width + x];
			puiAbgr[y * vkExtent3D.width + x] = ((argb & 0x00FF0000) >> 16) | ((argb & 0x0000FF00) >> 0) | ((argb & 0x000000FF) << 16);
		}
	}

	static int64_t siScreenshot = 1;
	int64_t iScreenshot = siScreenshot++;
	static std::future<void> sSaveScreenshot;
	sSaveScreenshot = std::async(std::launch::async, [rgba = move(rgba), puiAbgr, vkExtent3D, iScreenshot]() mutable
	{
		char pcDirectory[MAX_PATH] {};
		GetTempPath(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);
	
		std::string filename(pcDirectory);
		filename /= "Screenshots\\";
		filename += std::to_string(iScreenshot);
		filename += ".jpg";
		stbi_write_jpg(filename.c_str(), vkExtent3D.width, vkExtent3D.height, 4, puiAbgr, 80);
	});

	vkUnmapMemory(gpDeviceManager->mVkDevice, texture.mVkDeviceMemory);
}

} // namespace engine

#endif
