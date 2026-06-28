#include "Screenshot.h"

#include "Graphics/Managers/TextureCache.h"

#include "../../../ThirdParty/stb/stb_image_write.h"

namespace engine
{

void SaveScreenshot(int64_t iFramebufferIndex)
{
	LOG(kGraphics, kDebug, "SaveScreenshot()");

	// Wait on the fence for the specific framebuffer's Image command buffer.
	// Precondition: the fence must already have a pending or completed signal for the content being captured —
	// never a fresh reset whose signal depends on this caller returning (that was the screenshot deadlock). The
	// caller (Graphics::RenderMainPresentAcquire) guarantees this by running after SubmitUiCommandBuffer has
	// enqueued the UI submit that signals mVkFence (ImGuiManager::Submit) and before Present.
	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));

	VkExtent3D vkExtent3D {static_cast<uint32_t>(gpGraphics->mFramebufferExtent2D.width), static_cast<uint32_t>(gpGraphics->mFramebufferExtent2D.height), 1};

	// Read swapchain image data from GPU
	std::vector<std::byte> data;
	TextureCache::CopyImageToHostMemory(gpSwapchainManager->mFramebuffers.at(iFramebufferIndex).presentVkImage, vkExtent3D, gpInstanceManager->mFramebufferVkFormat, 1, 1, true, data);

	// Async save to disk
	static int64_t siScreenshot = 1;
	int64_t iScreenshot = siScreenshot++;
	static std::future<void> sSaveScreenshot;

	// Heap: the std::async shared-state + worker thread (and the previous future's teardown) escape into the
	// screenshot save thread, so the workbuffer cannot hold them. Suppression is thread-local and covers the rest
	// of this (synchronous) function; the lambda body runs on the screenshot thread (own ThreadLocal, untracked).
	// Main-loop-reachable per frame via the kbScreenshots trigger (Graphics::RenderMainPresentAcquire -> here);
	// dev-only toggle. (CopyImageToHostMemory's data.resize + staging buffer are already suppressed in TextureCache.)
	ScopedSuppressAllocationTracking suppress;
	if (sSaveScreenshot.valid())
	{
		sSaveScreenshot.get();
	}
	sSaveScreenshot = std::async(std::launch::async, [data = std::move(data), vkExtent3D, iScreenshot]() mutable
	{
		common::ThreadLocal threadLocal(0, common::kThreadScreenshot);

		// Convert pixel format from ARGB to RGBA by swapping red and blue channels
		const uint32_t* puiArgb = reinterpret_cast<const uint32_t*>(data.data());
		std::vector<uint32_t> rgba(vkExtent3D.width * vkExtent3D.height);
		uint32_t* puiAbgr = rgba.data();
		// Iterate through all pixels and rearrange color channels
		for (uint32_t y = 0; y < vkExtent3D.height; ++y)
		{
			for (uint32_t x = 0; x < vkExtent3D.width; ++x)
			{
				uint32_t argb = puiArgb[y * vkExtent3D.width + x];
				puiAbgr[y * vkExtent3D.width + x] = ((argb & 0x00FF0000) >> 16) | ((argb & 0x0000FF00) >> 0) | ((argb & 0x000000FF) << 16) | 0xFF000000;
			}
		}

		// Save to .jpg
		wchar_t pcDirectory[MAX_PATH] {};
		GetTempPathW(static_cast<DWORD>(std::size(pcDirectory) - 1), pcDirectory);

		std::filesystem::path filename(pcDirectory);
		filename /= "Screenshots";
		std::filesystem::create_directories(filename);
		filename /= std::format("{}.jpg", iScreenshot);
		LOG(kGraphics, kDebug, "  {}", filename);
		stbi_write_jpg(reinterpret_cast<const char*>(filename.u8string().c_str()), vkExtent3D.width, vkExtent3D.height, 4, puiAbgr, 80);
	});
}

} // namespace engine
