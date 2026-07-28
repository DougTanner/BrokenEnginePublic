#if defined(BT_CLIENT)

#include "Screenshot.h"

#include "Graphics/Managers/TextureCache.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

namespace
{

// Thread-safe hand-off of the finished agent "result" JSON from the async save/encode thread to the main-thread
// deferred-response poll. There is one active agent request/result slot, but abandoned screenshot and dump encoders
// may overlap because their independent futures outlive timeout/disconnect. The active token rejects their late
// publications so they cannot replace the newer request's result.
std::mutex sCaptureResultMutex;
std::optional<nlohmann::json> sCaptureResult;
uint64_t sCaptureTokenNext = 0; // monotonic mint (guarded by sCaptureResultMutex)
uint64_t suiCaptureTokenActive = 0; // current request token (guarded by sCaptureResultMutex)

std::u8string PathToU8(const std::filesystem::path& rPath)
{
	return rPath.u8string();
}

std::string PathToString(const std::filesystem::path& rPath)
{
	std::u8string u8 = rPath.u8string();
	return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
}

void ReportCaptureFailure(std::string_view error, bool bPublishResult, uint64_t uiCaptureToken)
{
	LOG(kGraphics, kWarning, "{}", error);
	if (bPublishResult)
	{
		nlohmann::json result;
		result["error"] = error;
		SetCaptureResult(uiCaptureToken, std::move(result));
	}
}

constexpr bool IsBgra(VkFormat vkFormat);

} // namespace

uint64_t ResetCaptureResult()
{
	std::unique_lock lock(sCaptureResultMutex);
	sCaptureResult.reset();
	suiCaptureTokenActive = ++sCaptureTokenNext;
	return suiCaptureTokenActive;
}

void SetCaptureResult(uint64_t uiCaptureToken, nlohmann::json result)
{
	std::unique_lock lock(sCaptureResultMutex);
	if (uiCaptureToken != suiCaptureTokenActive)
	{
		return;
	}
	sCaptureResult = std::move(result);
}

std::optional<nlohmann::json> TakeCaptureResult(uint64_t uiCaptureToken)
{
	std::unique_lock lock(sCaptureResultMutex);
	if (uiCaptureToken != suiCaptureTokenActive || !sCaptureResult.has_value())
	{
		return std::nullopt;
	}
	std::optional<nlohmann::json> result = std::move(sCaptureResult);
	sCaptureResult.reset();
	return result;
}

void SaveScreenshot(int64_t iFramebufferIndex, const ScreenshotRequest& rRequest)
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
	TextureCache::CopyImageToHostMemory(gpSwapchainManager->mFramebuffers.at(iFramebufferIndex).presentVkImage, vkExtent3D, gpInstanceManager->mFramebufferVkFormat, 1, 1, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, data);

	// Async save to disk. Default path (empty request path): %TEMP%\Screenshots\agent_{N}.{jpg|png}.
	static int64_t siScreenshot = 1;
	int64_t iScreenshot = siScreenshot++;
	static std::future<void> sSaveScreenshot;

	// Heap: the std::async shared-state + worker thread (and the previous future's teardown) escape into the
	// screenshot save thread, so the workbuffer cannot hold them. Suppression is thread-local and covers the rest
	// of this (synchronous) function; the lambda body runs on the screenshot thread (own ThreadLocal, untracked).
	// Main-loop-reachable per frame via the kbScreenshots trigger (Graphics::RenderMainPresentAcquire -> here).
	// (CopyImageToHostMemory's data.resize + staging buffer are already suppressed in TextureCache.)
	ScopedSuppressAllocationTracking suppress;
	if (sSaveScreenshot.valid())
	{
		sSaveScreenshot.get();
	}
	const bool bSwapRedBlue = IsBgra(gpInstanceManager->mFramebufferVkFormat);
	sSaveScreenshot = std::async(std::launch::async, [data = std::move(data), vkExtent3D, iScreenshot, rRequest, bSwapRedBlue]() mutable
	{
		common::ThreadLocal threadLocal(0, common::kThreadScreenshot);

		try
		{
			// Swap red/blue only for BGRA swapchains (the negotiated format can be RGBA or BGRA — InstanceManager
			// accepts either); alpha is forced opaque either way
			const uint32_t* puiArgb = reinterpret_cast<const uint32_t*>(data.data());
			std::vector<uint32_t> rgba(vkExtent3D.width * vkExtent3D.height);
			uint32_t* puiAbgr = rgba.data();
			for (uint32_t y = 0; y < vkExtent3D.height; ++y)
			{
				for (uint32_t x = 0; x < vkExtent3D.width; ++x)
				{
					uint32_t argb = puiArgb[y * vkExtent3D.width + x];
					puiAbgr[y * vkExtent3D.width + x] = bSwapRedBlue
						? ((argb & 0x00FF0000) >> 16) | ((argb & 0x0000FF00) >> 0) | ((argb & 0x000000FF) << 16) | 0xFF000000
						: (argb & 0x00FFFFFF) | 0xFF000000;
				}
			}

			int64_t iWidth = static_cast<int64_t>(vkExtent3D.width);
			int64_t iHeight = static_cast<int64_t>(vkExtent3D.height);
			const uint32_t* pPixels = puiAbgr;

			// Downscale preserving aspect when wider than iMaxWidth (for vision-model consumption).
			std::vector<uint32_t> resized;
			if (rRequest.iMaxWidth > 0 && iWidth > rRequest.iMaxWidth)
			{
				int64_t iNewWidth = rRequest.iMaxWidth;
				int64_t iNewHeight = std::max<int64_t>(1, (iHeight * iNewWidth) / iWidth);
				resized.resize(static_cast<size_t>(iNewWidth * iNewHeight));
				stbir_resize_uint8_srgb(reinterpret_cast<const unsigned char*>(puiAbgr), static_cast<int>(iWidth), static_cast<int>(iHeight), 0,
					reinterpret_cast<unsigned char*>(resized.data()), static_cast<int>(iNewWidth), static_cast<int>(iNewHeight), 0, STBIR_4CHANNEL);
				pPixels = resized.data();
				iWidth = iNewWidth;
				iHeight = iNewHeight;
			}

			std::filesystem::path filename;
			if (!rRequest.path.empty())
			{
				filename = rRequest.path;
			}
			else
			{
				wchar_t pcDirectory[MAX_PATH] {};
				uint32_t uiTemporaryPathLength = GetTempPathW(static_cast<DWORD>(std::size(pcDirectory)), pcDirectory);
				if (uiTemporaryPathLength == 0 || uiTemporaryPathLength >= std::size(pcDirectory))
				{
					ReportCaptureFailure("SaveScreenshot GetTempPathW failed or returned insufficient capacity", rRequest.bPublishResult, rRequest.uiCaptureToken);
					return;
				}
				filename = pcDirectory;
				filename /= "Screenshots";
				std::filesystem::create_directories(filename);
				filename /= std::format("agent_{}.{}", iScreenshot, rRequest.bPng ? "png" : "jpg");
			}
			LOG(kGraphics, kDebug, "  {}", filename);

			if (rRequest.bPng)
			{
				if (stbi_write_png(reinterpret_cast<const char*>(PathToU8(filename).c_str()), static_cast<int>(iWidth), static_cast<int>(iHeight), 4, pPixels, static_cast<int>(iWidth * 4)) == 0)
				{
					ReportCaptureFailure("SaveScreenshot stbi_write_png failed", rRequest.bPublishResult, rRequest.uiCaptureToken);
					return;
				}
			}
			else
			{
				if (stbi_write_jpg(reinterpret_cast<const char*>(PathToU8(filename).c_str()), static_cast<int>(iWidth), static_cast<int>(iHeight), 4, pPixels, static_cast<int>(rRequest.iQuality)) == 0)
				{
					ReportCaptureFailure("SaveScreenshot stbi_write_jpg failed", rRequest.bPublishResult, rRequest.uiCaptureToken);
					return;
				}
			}

			// Publish to the agent capture-result slot only for agent-originated requests; an F9 dev save must not
			// have its result consumed as the response to a concurrent agent capture.
			if (rRequest.bPublishResult)
			{
				nlohmann::json result;
				result["path"] = PathToString(filename);
				result["width"] = iWidth;
				result["height"] = iHeight;
				SetCaptureResult(rRequest.uiCaptureToken, std::move(result));
			}
		}
		catch (const std::exception& rException)
		{
			LOG(kGraphics, kError, "SaveScreenshot failed: {}", rException.what());
			if (rRequest.bPublishResult)
			{
				nlohmann::json result;
				result["error"] = rException.what();
				SetCaptureResult(rRequest.uiCaptureToken, std::move(result));
			}
		}
	});
}

namespace
{

// Throws if iIndex is outside [0, iCount); otherwise returns it as an array subscript.
size_t CheckDumpIndex(int64_t iIndex, int64_t iCount, const std::string& rName)
{
	if (iIndex < 0 || iIndex >= iCount)
	{
		throw std::runtime_error("render target '" + rName + "' index out of range [0, " + std::to_string(iCount) + ")");
	}
	return static_cast<size_t>(iIndex);
}

constexpr const char* kpcValidDumpNames =
	"Log, TerrainElevation, SmokeGradient, SmokeOne, SmokeTwo, WindOne, WindTwo, "
	"Lighting[0-2], Combine[0-2], AmbientCombine, LightingHistory[0-2], AmbientHistory, "
	"Spread[0-N][0-2], SpreadOnly[0-N][0-2], "
	"ShadowElevation, Shadow, ShadowBlur, ShadowBlurIntermediate, ShadowHistory, "
	"ObjectShadows, ObjectShadowsBlur, ObjectShadowsBlurIntermediate, WaterDisplacement, WaterDisplacementNormal";

// Resolve a dump name (+ indices) to a live RenderTargetTextures member. Throws std::runtime_error listing valid
// names on an unknown name, or on an out-of-range index (both trust-boundary errors surfaced to the agent).
Texture* ResolveRenderTarget(const std::string& rName, int64_t iIndex, int64_t iChannel)
{
	RenderTargetTextures& r = gpTextureManager->mRenderTargetTextures;

	if (rName == "Log")
	{
		return &r.mLogTexture;
	}
	if (rName == "TerrainElevation")
	{
		return &r.mTerrainElevationTexture;
	}
	if (rName == "SmokeGradient")
	{
		return &r.mSmokeGradientTexture;
	}
	if (rName == "SmokeOne")
	{
		return &r.mSmokeTextureOne;
	}
	if (rName == "SmokeTwo")
	{
		return &r.mSmokeTextureTwo;
	}
	if (rName == "WindOne")
	{
		return &r.mWindTextureOne;
	}
	if (rName == "WindTwo")
	{
		return &r.mWindTextureTwo;
	}
	if (rName == "AmbientCombine")
	{
		return &r.mAmbientCombineTexture;
	}
	if (rName == "AmbientHistory")
	{
		return &r.mAmbientHistoryTexture;
	}
	if (rName == "ShadowElevation")
	{
		return &r.mShadowElevationTexture;
	}
	if (rName == "Shadow")
	{
		return &r.mShadowTexture;
	}
	if (rName == "ShadowBlur")
	{
		return &r.mShadowBlurTexture;
	}
	if (rName == "ShadowBlurIntermediate")
	{
		return &r.mShadowBlurIntermediateTexture;
	}
	if (rName == "ShadowHistory")
	{
		return &r.mShadowHistoryTexture;
	}
	if (rName == "ObjectShadows")
	{
		return &r.mObjectShadowsTexture;
	}
	if (rName == "ObjectShadowsBlur")
	{
		return &r.mObjectShadowsBlurTexture;
	}
	if (rName == "ObjectShadowsBlurIntermediate")
	{
		return &r.mObjectShadowsBlurIntermediateTexture;
	}
	if (rName == "WaterDisplacement")
	{
		return &r.mWaterDisplacementTexture;
	}
	if (rName == "WaterDisplacementNormal")
	{
		return &r.mWaterDisplacementNormalTexture;
	}

	if (rName == "Lighting")
	{
		return &r.mpLightingTextures[CheckDumpIndex(iIndex, 3, rName)];
	}
	if (rName == "Combine")
	{
		return &r.mpCombineTextures[CheckDumpIndex(iIndex, 3, rName)];
	}
	if (rName == "LightingHistory")
	{
		return &r.mpLightingHistoryTextures[CheckDumpIndex(iIndex, 3, rName)];
	}

	if (rName == "Spread")
	{
		return &r.mpSpreadTextures[CheckDumpIndex(iIndex, shaders::kiMaxSpreadPasses, rName)][CheckDumpIndex(iChannel, 3, rName)];
	}
	if (rName == "SpreadOnly")
	{
		return &r.mpSpreadOnlyTextures[CheckDumpIndex(iIndex, shaders::kiMaxSpreadPasses, rName)][CheckDumpIndex(iChannel, 3, rName)];
	}

	throw std::runtime_error("unknown render target '" + rName + "'; valid names: " + kpcValidDumpNames);
}

const char* FormatName(VkFormat vkFormat)
{
	switch (vkFormat)
	{
		case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
		case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
		case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
		case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
		case VK_FORMAT_R16_UNORM: return "R16_UNORM";
		case VK_FORMAT_R16_SFLOAT: return "R16_SFLOAT";
		case VK_FORMAT_R32_SFLOAT: return "R32_SFLOAT";
		case VK_FORMAT_R16G16_SFLOAT: return "R16G16_SFLOAT";
		case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
		default: return "unsupported";
	}
}

constexpr bool IsFourByteColor(VkFormat vkFormat)
{
	return vkFormat == VK_FORMAT_R8G8B8A8_UNORM || vkFormat == VK_FORMAT_R8G8B8A8_SRGB
		|| vkFormat == VK_FORMAT_B8G8R8A8_UNORM || vkFormat == VK_FORMAT_B8G8R8A8_SRGB;
}

constexpr bool IsBgra(VkFormat vkFormat)
{
	return vkFormat == VK_FORMAT_B8G8R8A8_UNORM || vkFormat == VK_FORMAT_B8G8R8A8_SRGB;
}

constexpr bool IsSingleChannelNormalizable(VkFormat vkFormat)
{
	return vkFormat == VK_FORMAT_R16_UNORM || vkFormat == VK_FORMAT_R16_SFLOAT || vkFormat == VK_FORMAT_R32_SFLOAT;
}

// Convert one single-channel texel (per format) to float, for min/max normalization.
float SingleChannelToFloat(const std::byte* pData, int64_t iTexel, VkFormat vkFormat)
{
	if (vkFormat == VK_FORMAT_R16_UNORM)
	{
		uint16_t uiValue = reinterpret_cast<const uint16_t*>(pData)[iTexel];
		return static_cast<float>(uiValue) / static_cast<float>(std::numeric_limits<uint16_t>::max());
	}
	if (vkFormat == VK_FORMAT_R16_SFLOAT)
	{
		uint16_t uiValue = reinterpret_cast<const uint16_t*>(pData)[iTexel];
		return DirectX::PackedVector::XMConvertHalfToFloat(static_cast<DirectX::PackedVector::HALF>(uiValue));
	}
	// VK_FORMAT_R32_SFLOAT
	return reinterpret_cast<const float*>(pData)[iTexel];
}

// Async body: encode readback bytes to PNG (+ optional raw .bin) and publish the capture result. Runs on the
// screenshot thread (own untracked ThreadLocal). Only formats pre-validated in ValidateDumpRenderTargetRequest
// reach the PNG paths; any unexpected failure publishes an error result so the deferred poll never hangs.
void EncodeAndWriteDump(std::vector<std::byte>& rData, VkExtent3D vkExtent3D, VkFormat vkFormat, const DumpRenderTargetRequest& rRequest)
{
	try
	{
		int64_t iWidth = static_cast<int64_t>(vkExtent3D.width);
		int64_t iHeight = static_cast<int64_t>(vkExtent3D.height);
		int64_t iPixels = iWidth * iHeight;

		// Base path without extension: explicit request path (extension stripped) or default %TEMP% path.
		std::filesystem::path basePath;
		if (!rRequest.path.empty())
		{
			basePath = rRequest.path;
			basePath.replace_extension();
		}
		else
		{
			static std::atomic<int64_t> siDump {1};
			int64_t iDump = siDump.fetch_add(1);
			wchar_t pcDirectory[MAX_PATH] {};
			uint32_t uiTemporaryPathLength = GetTempPathW(static_cast<DWORD>(std::size(pcDirectory)), pcDirectory);
			if (uiTemporaryPathLength == 0 || uiTemporaryPathLength >= std::size(pcDirectory))
			{
				ReportCaptureFailure("DumpRenderTarget GetTempPathW failed or returned insufficient capacity", rRequest.bPublishResult, rRequest.uiCaptureToken);
				return;
			}
			basePath = pcDirectory;
			basePath /= "Screenshots";
			std::filesystem::create_directories(basePath);
			basePath /= std::format("dump_{}_{}", rRequest.name, iDump);
		}

		nlohmann::json result;
		result["format"] = FormatName(vkFormat);
		result["width"] = iWidth;
		result["height"] = iHeight;

		if (rRequest.bRaw)
		{
			std::filesystem::path binPath = basePath;
			binPath += ".bin";
			std::ofstream binStream(binPath, std::ios::binary);
			binStream.write(reinterpret_cast<const char*>(rData.data()), static_cast<std::streamsize>(rData.size()));
			binStream.close();
			result["raw"] = PathToString(binPath);
		}

		std::filesystem::path pngPath = basePath;
		pngPath += ".png";

		if (IsFourByteColor(vkFormat))
		{
			std::vector<uint32_t> rgba(static_cast<size_t>(iPixels));
			const uint32_t* pSrc = reinterpret_cast<const uint32_t*>(rData.data());
			if (IsBgra(vkFormat))
			{
				for (int64_t i = 0; i < iPixels; ++i)
				{
					uint32_t bgra = pSrc[i];
					rgba[i] = ((bgra & 0x00FF0000) >> 16) | ((bgra & 0x0000FF00) >> 0) | ((bgra & 0x000000FF) << 16) | (bgra & 0xFF000000);
				}
			}
			else
			{
				std::memcpy(rgba.data(), pSrc, static_cast<size_t>(iPixels) * sizeof(uint32_t));
			}
			if (stbi_write_png(reinterpret_cast<const char*>(PathToU8(pngPath).c_str()), static_cast<int>(iWidth), static_cast<int>(iHeight), 4, rgba.data(), static_cast<int>(iWidth * 4)) == 0)
			{
				ReportCaptureFailure("DumpRenderTarget color stbi_write_png failed", rRequest.bPublishResult, rRequest.uiCaptureToken);
				return;
			}
			result["path"] = PathToString(pngPath);
		}
		else if (IsSingleChannelNormalizable(vkFormat))
		{
			float fMin = std::numeric_limits<float>::max();
			float fMax = std::numeric_limits<float>::lowest();
			for (int64_t i = 0; i < iPixels; ++i)
			{
				float fValue = SingleChannelToFloat(rData.data(), i, vkFormat);
				fMin = std::min(fMin, fValue);
				fMax = std::max(fMax, fValue);
			}
			float fRange = fMax - fMin;
			if (fRange <= 0.0f)
			{
				fRange = 1.0f;
			}

			std::vector<uint8_t> gray(static_cast<size_t>(iPixels));
			for (int64_t i = 0; i < iPixels; ++i)
			{
				float fValue = SingleChannelToFloat(rData.data(), i, vkFormat);
				float fNormalized = std::clamp((fValue - fMin) / fRange, 0.0f, 1.0f);
				gray[i] = static_cast<uint8_t>(std::lround(fNormalized * 255.0f));
			}
			if (stbi_write_png(reinterpret_cast<const char*>(PathToU8(pngPath).c_str()), static_cast<int>(iWidth), static_cast<int>(iHeight), 1, gray.data(), static_cast<int>(iWidth)) == 0)
			{
				ReportCaptureFailure("DumpRenderTarget grayscale stbi_write_png failed", rRequest.bPublishResult, rRequest.uiCaptureToken);
				return;
			}
			result["path"] = PathToString(pngPath);
			result["min"] = fMin;
			result["max"] = fMax;
		}
		// else: format not PNG-encodable; pre-validation guarantees rRequest.bRaw, so the .bin above is the output.

		LOG(kGraphics, kDebug, "DumpRenderTarget {} -> {}", rRequest.name, basePath);
		if (rRequest.bPublishResult)
		{
			SetCaptureResult(rRequest.uiCaptureToken, std::move(result));
		}
	}
	catch (const std::exception& rException)
	{
		LOG(kGraphics, kError, "DumpRenderTarget failed: {}", rException.what());
		if (rRequest.bPublishResult)
		{
			nlohmann::json result;
			result["error"] = rException.what();
			SetCaptureResult(rRequest.uiCaptureToken, std::move(result));
		}
	}
}

} // namespace

void ValidateDumpRenderTargetRequest(const DumpRenderTargetRequest& rRequest)
{
	Texture* pTexture = ResolveRenderTarget(rRequest.name, rRequest.iIndex, rRequest.iChannel);

	// Readback requires the source image carry TRANSFER_SRC usage (CopyImageToHostMemory issues a transfer read).
	if ((pTexture->mInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
	{
		throw std::runtime_error("render target '" + rRequest.name + "' is not readback-capable (image lacks TRANSFER_SRC usage)");
	}

	// PNG encoding supports 4x8-bit color and single-channel (normalized) formats; other formats need raw:true.
	if (!IsFourByteColor(pTexture->mInfo.format) && !IsSingleChannelNormalizable(pTexture->mInfo.format) && !rRequest.bRaw)
	{
		throw std::runtime_error(std::string("render target format ") + FormatName(pTexture->mInfo.format) + " is not PNG-encodable; pass raw:true to dump the raw texels");
	}
}

void DumpRenderTarget(int64_t iFramebufferIndex, const DumpRenderTargetRequest& rRequest)
{
	LOG(kGraphics, kDebug, "DumpRenderTarget({})", rRequest.name);

	// Wait the per-framebuffer fence exactly as SaveScreenshot does (same precondition: the UI submit that signals
	// mVkFence is already enqueued at this capture site, never a fresh reset depending on this caller returning).
	// This fully drains the frame's compute/transfer/graphics work, so the readback below is safe regardless of the
	// target's last-access scope — CopyImageToHostMemory's srcStage (FRAGMENT_SHADER on the non-swapchain path)
	// cannot chain from a compute/transfer final transition (Shadow at GENERAL, Combine and ShadowHistory written by
	// compute, lighting-history transfer copies), so the fence wait is what serializes them, not the barrier.
	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iFramebufferIndex);
	CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));

	// Re-resolve (request was validated when queued). The fence wait above guarantees all passes writing this
	// target have completed.
	Texture* pTexture = ResolveRenderTarget(rRequest.name, rRequest.iIndex, rRequest.iChannel);

	VkExtent3D vkExtent3D = pTexture->mInfo.extent;
	VkFormat vkFormat = pTexture->mInfo.format;

	// Read the source's parked layout from its creation info rather than assuming SHADER_READ_ONLY — most render
	// targets park at SHADER_READ_ONLY, but Shadow (eTextureLayout kComputeReadWrite) parks at GENERAL, so a
	// hard-coded oldLayout would be spec-invalid and corrupt the next frame. Contract for every dumpable target:
	// it must be parked at mInfo.eTextureLayout's layout at this capture site.
	VkImageLayout vkCurrentLayout = ToVkImageLayout(pTexture->mInfo.eTextureLayout);

	std::vector<std::byte> data;
	TextureCache::CopyImageToHostMemory(pTexture->mVkImage, vkExtent3D, vkFormat, 1, 1, false, vkCurrentLayout, data);

	static std::future<void> sDump;

	// Heap: as SaveScreenshot — the std::async shared-state + moved-in readback bytes escape onto the dump thread,
	// so they cannot use the workbuffer; suppression covers the rest of this synchronous function.
	ScopedSuppressAllocationTracking suppress;
	if (sDump.valid())
	{
		sDump.get();
	}
	sDump = std::async(std::launch::async, [data = std::move(data), vkExtent3D, vkFormat, rRequest]() mutable
	{
		common::ThreadLocal threadLocal(0, common::kThreadScreenshot);
		EncodeAndWriteDump(data, vkExtent3D, vkFormat, rRequest);
	});
}

} // namespace engine

#endif // defined(BT_CLIENT)
