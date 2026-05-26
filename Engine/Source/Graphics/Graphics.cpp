#if defined(BT_CLIENT)

#include "Graphics.h"

#include "Game.h"
#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

std::tuple<int64_t, int64_t> FullDetail()
{
	// Block size multiplier must match max iShadowTextureY divisor
	int64_t iBlockSize = 8 * static_cast<int64_t>(shaders::kiShadowTextureExecutionSize);

	int64_t iX = iBlockSize;
	while ((10 * iX) / 9 < static_cast<int64_t>(gpGraphics->mFramebufferExtent2D.width))
	{
		iX += iBlockSize;
	}

	int64_t iY = iBlockSize;
	while ((10 * iY) / 9 < static_cast<int64_t>(gpGraphics->mFramebufferExtent2D.height))
	{
		iY += iBlockSize;
	}

	static int64_t siX = 0;
	static int64_t siY = 0;
	if (iX != siX || iY != siY)
	{
		LOG(kGraphics, kDebug, "FullDetail: {} x {}", iX, iY);
		siX = iX;
		siY = iY;
	}

	return std::make_tuple(iX, iY);
}

std::tuple<int64_t, int64_t> WaterFullDetail()
{
	// Gerstner frequencies are fixed, so the water vertex grid must be fixed too — anchor to a
	// reference 4K resolution instead of the live framebuffer extent. Block-snap matches FullDetail()
	// so the snapped result is deterministic and divides cleanly for shadow-execution-aligned consumers.
	constexpr int64_t kiReferenceWidth  = 3840;
	constexpr int64_t kiReferenceHeight = 2160;
	int64_t iBlockSize = 8 * static_cast<int64_t>(shaders::kiShadowTextureExecutionSize);

	int64_t iX = iBlockSize;
	while ((10 * iX) / 9 < kiReferenceWidth)
	{
		iX += iBlockSize;
	}

	int64_t iY = iBlockSize;
	while ((10 * iY) / 9 < kiReferenceHeight)
	{
		iY += iBlockSize;
	}

	return std::make_tuple(iX, iY);
}

float SmokeSimulationPixels()
{
	static constexpr float kfReferencePixels = 3840.0f;
	float fPixels = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	return (fPixels / kfReferencePixels) * 8192.0f * gSmokeSimulationPixels.Get();
}

float SmokeSimulationPixelsY()
{
	// Floor to a multiple of kiComputeTileSize so uiSmokeTilesY divides evenly
	float fWidth = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	float fHeight = static_cast<float>(gpGraphics->mFramebufferExtent2D.height);
	float fY = SmokeSimulationPixels() * (fHeight / fWidth);
	return std::floor(fY / static_cast<float>(shaders::kiComputeTileSize)) * static_cast<float>(shaders::kiComputeTileSize);
}

static void CheckVulkan12Support()
{
	// vkEnumerateInstanceVersion was added in Vulkan 1.1
	// If the function pointer is null, we're on Vulkan 1.0
	if (vkEnumerateInstanceVersion == nullptr)
	{
		MessageBox(nullptr, "Vulkan 1.2 or higher is required.\n\nYour graphics driver only supports Vulkan 1.0.", game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("Vulkan 1.2 not available");
	}

	// Query the Vulkan API version and verify it's at least 1.2
	uint32_t uiApiVersion = 0;
	VkResult vkResult = vkEnumerateInstanceVersion(&uiApiVersion);
	if (vkResult != VK_SUCCESS || uiApiVersion < VK_API_VERSION_1_2)
	{
		uint32_t uiMajor = VK_VERSION_MAJOR(uiApiVersion);
		uint32_t uiMinor = VK_VERSION_MINOR(uiApiVersion);

		std::string errorMessage = "Vulkan 1.2 or higher is required.\n\nYour graphics driver supports Vulkan ";
		errorMessage += std::to_string(uiMajor);
		errorMessage += ".";
		errorMessage += std::to_string(uiMinor);
		errorMessage += ".";

		MessageBox(nullptr, errorMessage.c_str(), game::kGameName.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
		throw std::runtime_error("Vulkan 1.2 not available");
	}
}

Graphics::Graphics(HINSTANCE hinstance, HWND hwnd)
: mHinstance(hinstance)
, mHwnd(hwnd)
{
	gpGraphics = this;

	CHECK_VK(volkInitialize());

	CheckVulkan12Support();

	Create();

	gpSwapchainManager->AcquireNextImage();

	// Find the monitor refresh rate
	int64_t iDevices = 0;
	DISPLAY_DEVICE displayDevice {.cb = sizeof(DISPLAY_DEVICE)};
	while (EnumDisplayDevices(nullptr, static_cast<DWORD>(iDevices++), &displayDevice, EDD_GET_DEVICE_INTERFACE_NAME) == TRUE)
	{
		DEVMODEA devmodea {};
		if ((displayDevice.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0 && EnumDisplaySettings(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devmodea) == TRUE)
		{
			LOG(kGraphics, kDebug, "Active display device \"{}\" has frequency of {} Hz", displayDevice.DeviceName, devmodea.dmDisplayFrequency);
			miMonitorRefreshRate = devmodea.dmDisplayFrequency;
		}
	}
}

Graphics::~Graphics()
{
	meDestroyType = DestroyType::kSurface;
	Destroy();

	gpGraphics = nullptr;
}

void Graphics::WaitAllFramebufferFencesIdle()
{
	// Drain ALL in-flight framebuffer fences (RenderGlobal already waited only the current one). This
	// is NOT vkDeviceWaitIdle — only graphics-queue work references the island slots being freed /
	// re-patched, so the present and transfer queues need not stall. The kExecuted guard skips fences
	// that were never submitted (early frames), which are not signalable.
	for (CommandBuffers& rCommandBuffers : gpCommandBufferManager->mPerFramebufferCommandBuffers)
	{
		if (rCommandBuffers.mFlags & CommandBufferFlags::kExecuted)
		{
			CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));
		}
	}
}

void Graphics::RenderGlobal(float fCurrentTime)
{
	int64_t iCommandBuffer = gpSwapchainManager->miFramebufferIndex;

	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(iCommandBuffer);
	VkResult vkResult = vkGetFenceStatus(gpDeviceManager->mVkDevice, rCommandBuffers.mVkFence);
	if (vkResult == VK_NOT_READY)
	{
		ScopedCpuProfile scopedCpuProfile(kCpuTimerWaitFence);
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mVkFence, VK_TRUE, kFenceTimeoutNanoseconds.count()));
	}
	else if (vkResult != VK_SUCCESS)
	{
		CHECK_VK(vkResult);
	}

	miRenderFrameDeltaNs = mRenderFrameTimer.GetDeltaNs(true).count();

	// Phase 5 LRU eviction sweeps bracket ProcessPendingTextures inside the descriptor-patch
	// safety window (post-fence-wait, pre-cmd-buffer-recording). EvictionSweep frees GPU
	// resources for templates whose grace period elapsed; RestorationSweep patches per-channel
	// from slot-0 fallback back to real Texture* as each chunk reaches kReady.
	//
	// The single current-framebuffer fence wait above is insufficient for these sweeps: with triple
	// buffering, OTHER in-flight frames may still reference the slots whose images/descriptors the
	// sweeps free or rewrite. Drain all framebuffer fences first — but only on frames where a sweep
	// will actually mutate (island-set churn), so steady-state frames pay no stall.
	//
	// ProcessPendingTextures (below) also writes descriptor elements on any frame it adopts a chunk —
	// per-slot island writes, the array flush, and the lighting-blur array write — which race in-flight
	// samplers the same way (UPDATE_AFTER_BIND makes the write spec-legal, not race-free). AnyAdoptionPending
	// folds those adoption frames into the same drain; restoration's template-owned elevation array write
	// has no mTextureMap chunk, so AnyRestorationPending stays a distinct, non-redundant predicate.
	if (gpIslandTerrain->AnyEvictionPending() || gpIslandTerrain->AnyRestorationPending() || gpTextureManager->AnyAdoptionPending())
	{
		WaitAllFramebufferFencesIdle();
	}
	gpIslandTerrain->EvictionSweep();

	// Process pending texture loads after fence wait when it's safe to update GPU resources
	gpTextureManager->ProcessPendingTextures(iCommandBuffer);

	gpIslandTerrain->RestorationSweep();

	// Update VMA frame index for memory budget tracking. muiFrameCounter must advance every frame
	// (independent of the budget extension) because Phase 5 LRU grace uses it as a monotonic clock.
	if (gpDeviceManager->mbMemoryBudgetAvailable)
	{
		vmaSetCurrentFrameIndex(gpDeviceManager->mpAllocator, static_cast<uint32_t>(muiFrameCounter));
	}
	++muiFrameCounter;

	if (rCommandBuffers.mFlags & CommandBufferFlags::kExecuted)
	{
		gpProfileManager->GpuRead(iCommandBuffer, kGpuTimerGlobal, kGpuTimerCount);
	}

	gpProfileManager->CpuStart(kCpuTimerRenderGlobal);
	RenderFrameGlobal(iCommandBuffer, fCurrentTime);
	gpParticleManager->RenderGlobal(iCommandBuffer);
	gpProfileManager->CpuStop(kCpuTimerRenderGlobal, false);

	gpCommandBufferManager->SubmitGlobalCommandBuffer(iCommandBuffer);
}

void Graphics::RenderMainPresentAcquire(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	gpProfileManager->CpuStart(kCpuTimerRenderMain);
	RenderFrameMain(iCommandBuffer, rRenderInterpolates, rActiveCoords, cameraCoord);
	gpProfileManager->CpuStop(kCpuTimerRenderMain, false);

	gpImGuiManager->Prepare(iCommandBuffer);

	gpCommandBufferManager->SubmitMainCommandBuffer(iCommandBuffer, false);

	gpCommandBufferManager->SubmitUiCommandBuffer(iCommandBuffer);

	gpSwapchainManager->Present(iCommandBuffer);

	// Signal upload thread to process one upload iteration
	gpTextureUploadManager->mFrameSignal.release();

	// Renders per second
	mRendersInTheLastSecond.Set();

	gpProfileManager->UpdateProfileText();

	if constexpr (kbRenderThread)
	{
		gpProfileManager->CpuStart(kCpuTimerWaitPresentFuture);
		gpSwapchainManager->mPresent.Wait();
		gpProfileManager->CpuStop(kCpuTimerWaitPresentFuture, false);
	}

	Create();

	gpSwapchainManager->AcquireNextImage();
	gpProfileManager->CpuStart(kCpuTimerAcquireToGlobal);
}

void Graphics::Create()
{
	// Heap: make_unique for each manager (~12 objects that live for the app's lifetime or until device loss).
	// Can't use workbuffer (temporary) or pre-allocate (managers have complex internal state built in constructors)
	ScopedSuppressAllocationTracking suppress;

	Refresh();
	Destroy();

	if (mpInstanceManager == nullptr)
	{
		mpInstanceManager = std::make_unique<InstanceManager>(mHinstance, mHwnd);
	}
	if (mpDeviceManager == nullptr)
	{
		mpDeviceManager = std::make_unique<DeviceManager>();
		gpTextureUploadManager->InitTransferResources();
	}
	bool bSwapchainRecreated = (mpSwapchainManager == nullptr);
	if (mpSwapchainManager == nullptr)
	{
		mpSwapchainManager = std::make_unique<SwapchainManager>(mOldVkSwapchainKHR);
		mOldVkSwapchainKHR = VK_NULL_HANDLE;
	}
	if constexpr (kbProfiling)
	{
		gpProfileManager->Create();
	}
	bool bRecordCommandBuffers = false;
	if (mpCommandBufferManager == nullptr)
	{
		mpCommandBufferManager = std::make_unique<CommandBufferManager>();
		bRecordCommandBuffers = true;
	}
	if (mpBufferManager == nullptr)
	{
		mpBufferManager = std::make_unique<BufferManager>();
	}
	else if (bSwapchainRecreated)
	{
		gpBufferManager->CreateSwapchainDependentBuffers();
	}
	if (mpIslands == nullptr)
	{
		mpIslands = std::make_unique<Islands>();
	}
	if (mpTextureManager == nullptr)
	{
		mpTextureManager = std::make_unique<TextureManager>();
	}
	else if (bSwapchainRecreated)
	{
		gpTextureManager->CreateScreenDependentResources();
	}
	if (mpTextManager == nullptr)
	{
		mpTextManager = std::make_unique<TextManager>();
	}
	if (mpPipelineManager == nullptr)
	{
		mpPipelineManager = std::make_unique<PipelineManager>();
	}
	if (mpParticleManager == nullptr)
	{
		mpParticleManager = std::make_unique<ParticleManager>();
	}
	if (mpImGuiManager == nullptr)
	{
		mpImGuiManager = std::make_unique<ImGuiManager>(mHwnd);

		if constexpr (kbDebugInput)
		{
			if (game::gpGame != nullptr)
			{
				game::Game::LoadTweaksSettings();
			}
		}
	}

	if (bRecordCommandBuffers)
	{
		gpCommandBufferManager->RecordCommandBuffers();
	}

}

void Graphics::Refresh()
{
	auto [bMultisampling, bPreviousMultisampling, bMultisamplingChanged] = gMultisampling.Changed<bool>();
	if (bMultisamplingChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Multisampling: {} -> {}", bPreviousMultisampling, bMultisampling);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gpInstanceManager != nullptr && gSampleCount.Get<VkSampleCountFlagBits>() > gpInstanceManager->meMaxMultisampleCount)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(gpInstanceManager->meMaxMultisampleCount);
	}

	auto [eSampleCount, ePreviousSampleCount, bSampleCountChanged] = gSampleCount.Changed<VkSampleCountFlagBits>();
	if (bSampleCountChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Sample count: {} -> {}", static_cast<int64_t>(ePreviousSampleCount), static_cast<int64_t>(eSampleCount));
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [ePresentMode, ePreviousPresentMode, bPresentModeChanged] = gPresentMode.Changed<VkPresentModeKHR>();
	if (bPresentModeChanged) [[unlikely]]
	{
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferAllocation<const char*> pcPreviousPresentMode = gEnumToString.Convert(ePreviousPresentMode, rWorkbuffer);
		common::ScopedWorkbufferAllocation<const char*> pcPresentMode = gEnumToString.Convert(ePresentMode, rWorkbuffer);
		LOG(kGraphics, kDebug, "{} -> {}", pcPreviousPresentMode, pcPresentMode);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gWantedFramebufferExtent2D.width != mFramebufferExtent2D.width || gWantedFramebufferExtent2D.height != mFramebufferExtent2D.height) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "{} x {} -> {} x {}", mFramebufferExtent2D.width, mFramebufferExtent2D.height, gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
		if (gWantedFramebufferExtent2D.width == 0 || gWantedFramebufferExtent2D.height == 0)
		{
			return;
		}
		mFramebufferExtent2D = gWantedFramebufferExtent2D;
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [bAnisotropy, bPreviousAnisotropy, bAnisotropyChanged] = gAnisotropy.Changed<bool>();
	if (bAnisotropyChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Anisotropy: {} -> {}", bPreviousAnisotropy, bAnisotropy);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [fMaxAnisotropy, fPreviousMaxAnisotropy, bMaxAnisotropyChanged] = gMaxAnisotropy.Changed<float>();
	if (bMaxAnisotropyChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Max anisotropy: {} -> {}", fPreviousMaxAnisotropy, fMaxAnisotropy);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [bSampleShading, bPreviousSampleShading, bSampleShadingChanged] = gSampleShading.Changed<bool>();
	if (bSampleShadingChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Sample shading: {} -> {}", bPreviousSampleShading, bSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMinSampleShading, fPreviousMinSampleShading, bMinSampleShadingChanged] = gMinSampleShading.Changed<float>();
	if (bMinSampleShadingChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Min sample shading: {} -> {}", fPreviousMinSampleShading, fMinSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMipLodBias, fPreviousMipLodBias, bMipLodBiasChanged] = gMipLodBias.Changed<float>();
	if (bMipLodBiasChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Mip lod bias: {} -> {}", fPreviousMipLodBias, fMipLodBias);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [bWireframe, bPreviousWireframe, bWireframeChanged] = gWireframe.Changed<bool>();
	if (bWireframeChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Wireframe: {} -> {}", bPreviousWireframe, bWireframe);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [bDebugTexture, bPreviousDebugTexture, bDebugTextureChanged] = gDebugTexture.Changed<bool>();
	if (bDebugTextureChanged) [[unlikely]]
	{
		meDestroyType = std::max(DestroyType::kCommandBuffers, meDestroyType);
	}

	auto [fWaterShapeDetail, fPreviousWaterShapeDetail, bWaterShapeDetailChanged] = gWaterShapeDetail.Changed<float>();
	if (bWaterShapeDetailChanged && gpBufferManager != nullptr) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Water shape detail: {} -> {}", fPreviousWaterShapeDetail, fWaterShapeDetail);

		mDestroyFlags.Set(DestroyFlags::kWaterMesh);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fShadowRenderMultiplier, fShadowRenderMultiplierPrevious, bShadowRenderMultiplierChanged] = gShadowRenderMultiplier.Changed<float>();
	if (bShadowRenderMultiplierChanged && gpTextureManager != nullptr) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "Shadow render multiplier: {} -> {}", common::Wb(fShadowRenderMultiplierPrevious, 3), common::Wb(fShadowRenderMultiplier, 3));

		mDestroyFlags.Set(DestroyFlags::kShadowTextures);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fLightingMultiplier, fLightingMultiplierPrevious, bLightingMultiplierChanged] = gLightingDepositTextureMultiplier.Changed<float>();
	auto [fSpreadTextureMultiplierStart, fSpreadTextureMultiplierStartPrevious, bSpreadTextureMultiplierStartChanged] = gSpreadTextureMultiplierStart.Changed<float>();
	auto [fSpreadTextureMultiplierEnd, fSpreadTextureMultiplierEndPrevious, bSpreadTextureMultiplierEndChanged] = gSpreadTextureMultiplierEnd.Changed<float>();
	auto [fSpreadPassCount, fSpreadPassCountPrevious, bSpreadPassCountChanged] = gSpreadPassCount.Changed<float>();
	if ((bLightingMultiplierChanged || bSpreadTextureMultiplierStartChanged || bSpreadTextureMultiplierEndChanged || bSpreadPassCountChanged) && gpTextureManager != nullptr) [[unlikely]]
	{
		mDestroyFlags.Set(DestroyFlags::kLightingTextures);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fLightingBlurSigma, fLightingBlurSigmaPrevious, bLightingBlurSigmaChanged] = gLightingBlurSigma.Changed<float>();
	auto [fLightingBlurSampleCount, fLightingBlurSampleCountPrevious, bLightingBlurSampleCountChanged] = gLightingBlurSampleCount.Changed<float>();
	auto [fLightingBlurEdgeFalloff, fLightingBlurEdgeFalloffPrevious, bLightingBlurEdgeFalloffChanged] = gLightingBlurEdgeFalloff.Changed<float>();
	if ((bLightingBlurSigmaChanged || bLightingBlurSampleCountChanged || bLightingBlurEdgeFalloffChanged) && gpTextureManager != nullptr) [[unlikely]]
	{
		gpTextureManager->ReblurAllLightingTextures();
	}

	auto [fObjectShadowsRenderMultiplier, fObjectShadowsRenderMultiplierPrevious, bObjectShadowsRenderMultiplierChanged] = gObjectShadowsRenderMultiplier.Changed<float>();
	auto [fObjectShadowsBlurMultiplier, fObjectShadowsBlurMultiplierPrevious, bObjectShadowsBlurMultiplierChanged] = gObjectShadowsBlurMultiplier.Changed<float>();
	if ((bObjectShadowsRenderMultiplierChanged || bObjectShadowsBlurMultiplierChanged) && gpTextureManager != nullptr) [[unlikely]]
	{
		mDestroyFlags.Set(DestroyFlags::kObjectShadows);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fWaterSkyboxOneRenderMultiplier, fWaterSkyboxOneRenderMultiplierPrevious, bWaterSkyboxOneRenderMultiplierChanged] = gWaterSkyboxOneRenderMultiplier.Changed<float>();
	if (bWaterSkyboxOneRenderMultiplierChanged && gpTextureManager != nullptr) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "WaterSkyboxOne render multiplier: {} -> {}", common::Wb(fWaterSkyboxOneRenderMultiplierPrevious, 3), common::Wb(fWaterSkyboxOneRenderMultiplier, 3));

		mDestroyFlags.Set(DestroyFlags::kWaterSkyboxOne);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	if (gpInstanceManager != nullptr) [[likely]]
	{
		auto [fTerrainElevationTextureMultiplier, fPreviousTerrainElevationTextureMultiplier, bTerrainElevationTextureMultiplierChanged] = gTerrainElevationTextureMultiplier.Changed<float>();
		if (bTerrainElevationTextureMultiplierChanged) [[unlikely]]
		{
			LOG(kGraphics, kDebug, "TerrainElevationTexture multiplier: {} -> {}", fPreviousTerrainElevationTextureMultiplier, fTerrainElevationTextureMultiplier);

			mDestroyFlags.Set(DestroyFlags::kTerrainElevation);

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fSmokeTrailPower, fSmokeTrailPowerPrevious, bSmokeTrailPowerChanged] = gSmokeTrailPower.Changed<float>();
		auto [fSmokeTrailAlpha, fSmokeTrailAlphaPrevious, bSmokeTrailAlphaChanged] = gSmokeTrailAlpha.Changed<float>();
		auto [fSmokeSimulationPixels, fPreviousSmokeSimulationPixels, bSmokeSimulationPixelsChanged] = gSmokeSimulationPixels.Changed<float>();
		auto [fSmokeSimulationArea, fPreviousSmokeSimulationArea, bSmokeSimulationAreaChanged] = gSmokeSimulationArea.Changed<float>();
		if (bSmokeTrailPowerChanged || bSmokeTrailAlphaChanged || bSmokeSimulationPixelsChanged || bSmokeSimulationAreaChanged) [[unlikely]]
		{
			LOG(kGraphics, kDebug, "SmokeSimulationPixels: {} -> {} ({} -> {})", fPreviousSmokeSimulationPixels, fSmokeSimulationPixels, gSmokeSimulationPixels.Get(), SmokeSimulationPixels());
			LOG(kGraphics, kDebug, "SmokeSimulationArea: {} -> {}", fPreviousSmokeSimulationArea, fSmokeSimulationArea);

			mDestroyFlags.Set(DestroyFlags::kSmokeTextures);
			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}
	}
}

void Graphics::RecreateResources()
{
	if (mDestroyFlags.Empty())
	{
		return;
	}

	if (mDestroyFlags & DestroyFlags::kShadowTextures)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateShadowTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kObjectShadows)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateObjectShadowsTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kWaterSkyboxOne)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateWaterSkyboxOneTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kLightingTextures)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateLightingTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kWaterMesh)
	{
		if (gpBufferManager != nullptr)
		{
			gpBufferManager->CreateWaterMesh();
		}
		if (gpTextureManager != nullptr)
		{
			// Texel grid must match the water-mesh vertex grid (see RenderTargetTextures.h comment).
			gpTextureManager->mRenderTargetTextures.mWaterDisplacementTexture.Destroy();
			gpTextureManager->mRenderTargetTextures.mWaterDisplacementNormalTexture.Destroy();
			gpTextureManager->mRenderTargetTextures.CreateWaterDisplacementTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kTerrainElevation)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateTerrainTextures();
		}
	}

	if (mDestroyFlags & DestroyFlags::kSmokeTextures)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->mRenderTargetTextures.CreateSmokeTextures();
			gpTextureManager->mRenderTargetTextures.CreateWindTextures();
		}
	}

	mDestroyFlags.ClearAll();
}

bool Graphics::Destroy()
{
	if (meDestroyType == DestroyType::kNone)
	{
		return false;
	}

	LOG(kGraphics, kInfo, "Graphics::Destroy() {}", static_cast<int64_t>(meDestroyType));

	// Drain worker threads (Vulkan requires exclusive host access to all queues)
	if (gpSwapchainManager != nullptr)
	{
		gpSwapchainManager->mPresent.Wait();
	}
	if (gpCommandBufferManager != nullptr)
	{
		gpCommandBufferManager->mSubmitMain.Wait();
		gpCommandBufferManager->mSubmitGlobal.Wait();
	}
	if (gpTextureUploadManager != nullptr)
	{
		gpTextureUploadManager->WaitIdle();
	}

	if (gpDeviceManager != nullptr)
	{
		vkDeviceWaitIdle(gpDeviceManager->mVkDevice);
	}

	// Only recreate resources if we're doing partial recreation (not full shutdown)
	RecreateResources();

	if (meDestroyType >= DestroyType::kSamplers)
	{
		if (gpTextureManager != nullptr)
		{
			gpTextureManager->DestroySamplers();
			gpTextureManager->CreateSamplers();

			// Global Set 0 survives pipeline recreation, always update it with new sampler handles
			gpTextureManager->mTextureDescriptors.WriteGlobalDescriptorSets();

			// Rewrite per-pipeline sampler descriptors unless all pipelines are being fully rebuilt
			if (meDestroyType < DestroyType::kPipelines)
			{
				gpTextureManager->mTextureDescriptors.RewriteSamplerDescriptors();
			}
		}
	}

	if (meDestroyType >= DestroyType::kCommandBuffers)
	{
		mpCommandBufferManager.reset();
	}

	if (meDestroyType >= DestroyType::kPipelines)
	{
		mpPipelineManager.reset();
	}

	if (meDestroyType >= DestroyType::kSwapchain)
	{
		if (meDestroyType < DestroyType::kSurface)
		{
			// Partial destroy: keep TextureManager and BufferManager alive, only destroy screen-dependent internals
			if (gpTextureManager != nullptr)
			{
				gpTextureManager->DestroyScreenDependentResources();
			}
			if (gpBufferManager != nullptr)
			{
				gpBufferManager->DestroySwapchainDependentBuffers();
			}
		}
		else
		{
			// Full destroy: tear down completely for device recreation
			mpTextureManager.reset();
			mpBufferManager.reset();
		}
		if constexpr (kbProfiling)
		{
			gpProfileManager->Destroy();
		}
		// Save old swapchain handle for seamless transition (only during recreation, not final shutdown)
		if (mpSwapchainManager != nullptr && meDestroyType < DestroyType::kSurface)
		{
			mOldVkSwapchainKHR = mpSwapchainManager->mVkSwapchainKHR;
			mpSwapchainManager->mVkSwapchainKHR = VK_NULL_HANDLE;
		}
		mpSwapchainManager.reset();
		if constexpr (kbDebugInput)
		{
			if (game::gpGame != nullptr)
			{
				game::Game::SaveTweaksSettings();
			}
		}
		mpImGuiManager.reset();
	}

	if (meDestroyType >= DestroyType::kSurface)
	{
		mpIslands.reset();
		gpTextureUploadManager->DestroyTransferResources();
		// Reset lazy-loaded texture chunk states so they reload after device recreation
		gpFileManager->ResetTextureChunkStates();
		// IslandTerrain is game-frame-owned (outlives Graphics) but its IslandTemplate::mMeshBuffer
		// allocations came from this allocator. Release them before mpDeviceManager.reset() to avoid
		// the VMA "allocations not freed before destruction" assertion on shutdown.
		if (gpIslandTerrain != nullptr)
		{
			gpIslandTerrain->ReleaseGpuResources();
		}
		mpDeviceManager.reset();
		mpInstanceManager.reset();
	}

	mDestroyFlags = DestroyFlags_t {};
	meDestroyType = DestroyType::kNone;

	return true;
}

} // namespace engine

#endif // defined(BT_CLIENT)
