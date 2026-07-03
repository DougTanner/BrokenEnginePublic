#if defined(BT_CLIENT)

#include "Graphics.h"

#include "Game.h"
#include "Profile/ProfileManager.h"
#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/WaterWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

// Shadow-execution-aligned snap block size. FullDetail and WaterFullDetail both snap their render extents
// to this multiple; single-sourced here so the two snap loops cannot drift apart.
constexpr int64_t kiDetailBlockSize = 8 * static_cast<int64_t>(shaders::kiShadowTextureExecutionSize);

// Reference render width. WaterFullDetail anchors the resolution-independent Gerstner water grid to it, and
// SmokeSimulationPixels scales the smoke-sim resolution relative to it.
constexpr int64_t kiReferenceWidth = 3840;
// Smoke-sim pixel budget at the reference width (smoke resolution scales by actual/reference width).
constexpr float kfSmokeReferencePixels = 8192.0f;

std::tuple<int64_t, int64_t> FullDetail()
{
	int64_t iBlockSize = kiDetailBlockSize;

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
	constexpr int64_t kiReferenceHeight = 2160;
	int64_t iBlockSize = kiDetailBlockSize;

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
	float fPixels = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	return (fPixels / static_cast<float>(kiReferenceWidth)) * kfSmokeReferencePixels * gSmokeSimulationPixels.Get();
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
	ASSERT(gpGraphics == nullptr);

	gpGraphics = this;

	CHECK_VK(volkInitialize());

	CheckVulkan12Support();

	Create();

	LoadAnimationDataFromEagerChunks();

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

	if (gpGraphics == this)
	{
		gpGraphics = nullptr;
	}
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
	if (gpDeviceManager->mCapabilities & DeviceCapabilityFlags::kMemoryBudgetAvailable)
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
	gpProfileManager->CpuStop(kCpuTimerRenderGlobal);

	gpCommandBufferManager->SubmitGlobalCommandBuffer(iCommandBuffer);
}

void Graphics::RenderMainPresentAcquire(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	gpProfileManager->CpuStart(kCpuTimerRenderMain);
	RenderFrameMain(iCommandBuffer, rRenderInterpolates, rActiveCoords, cameraCoord);
	gpProfileManager->CpuStop(kCpuTimerRenderMain);

	gpImGuiManager->Prepare(iCommandBuffer);

	gpCommandBufferManager->SubmitMainCommandBuffer(iCommandBuffer, false);

	gpCommandBufferManager->SubmitUiCommandBuffer(iCommandBuffer);

	// Capture between the UI submit and Present: SubmitUiCommandBuffer has enqueued the UI submit
	// (ImGuiManager::Submit) that signals the per-framebuffer fence, so SaveScreenshot's wait blocks on an
	// already-pending signal rather than a fresh reset whose signal depends on this thread returning (that was the
	// deadlock), and the present image is still application-owned. Costs a full GPU sync on screenshot frames;
	// dev-only toggle.
	if constexpr (kbScreenshots)
	{
		if (gpCommandBufferManager->mbSaveScreenshot)
		{
			gpCommandBufferManager->mbSaveScreenshot = false;
			SaveScreenshot(iCommandBuffer);
		}
	}

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
		gpProfileManager->CpuStop(kCpuTimerWaitPresentFuture);
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
				game::LoadTweaksSettings();
			}
		}
	}

	if (bRecordCommandBuffers)
	{
		gpCommandBufferManager->RecordCommandBuffers();
	}

}

template <typename T>
void Graphics::PollSetting(Wrapper& rWrapper, const char* pcLabel, DestroyType eTier, std::optional<DestroyFlags> oeFlag, bool bGate)
{
	auto [vCurrent, vPrevious, bChanged] = rWrapper.Changed<T>();
	if (bChanged && bGate) [[unlikely]]
	{
		if (pcLabel != nullptr)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				LOG(kGraphics, kDebug, "{}: {} -> {}", pcLabel, common::Wb(vPrevious, 3), common::Wb(vCurrent, 3));
			}
			else if constexpr (std::is_enum_v<T>)
			{
				LOG(kGraphics, kDebug, "{}: {} -> {}", pcLabel, static_cast<int64_t>(vPrevious), static_cast<int64_t>(vCurrent));
			}
			else
			{
				LOG(kGraphics, kDebug, "{}: {} -> {}", pcLabel, vPrevious, vCurrent);
			}
		}

		if (oeFlag.has_value())
		{
			mDestroyFlags.Set(oeFlag.value());
		}

		meDestroyType = std::max(eTier, meDestroyType);
	}
}

void Graphics::Refresh()
{
	PollSetting<bool>(gMultisampling, "Multisampling", DestroyType::kSwapchain);

	if (gpInstanceManager != nullptr && gSampleCount.Get<VkSampleCountFlagBits>() > gpInstanceManager->meMaxMultisampleCount)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(gpInstanceManager->meMaxMultisampleCount);
	}

	PollSetting<VkSampleCountFlagBits>(gSampleCount, "Sample count", DestroyType::kSwapchain);

	auto [ePresentMode, ePreviousPresentMode, bPresentModeChanged] = gPresentMode.Changed<VkPresentModeKHR>();
	if (bPresentModeChanged) [[unlikely]]
	{
		LOG(kGraphics, kDebug, "{} -> {}", string_VkPresentModeKHR(ePreviousPresentMode), string_VkPresentModeKHR(ePresentMode));
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

	PollSetting<bool>(gAnisotropy, "Anisotropy", DestroyType::kSamplers);
	PollSetting<float>(gMaxAnisotropy, "Max anisotropy", DestroyType::kSamplers);
	PollSetting<bool>(gSampleShading, "Sample shading", DestroyType::kPipelines);
	PollSetting<float>(gMinSampleShading, "Min sample shading", DestroyType::kPipelines);
	PollSetting<float>(gMipLodBias, "Mip lod bias", DestroyType::kSamplers);
	PollSetting<float>(gWaterNormalMipBias, "Water normal mip bias", DestroyType::kSamplers);
	PollSetting<bool>(gWireframe, "Wireframe", DestroyType::kPipelines);
	PollSetting<bool>(gDebugTexture, nullptr, DestroyType::kCommandBuffers);

	PollSetting<float>(gWaterShapeDetail, "Water shape detail", DestroyType::kPipelines, DestroyFlags::kWaterMesh, gpBufferManager != nullptr);

	PollSetting<float>(gShadowRenderMultiplier, "Shadow render multiplier", DestroyType::kPipelines, DestroyFlags::kShadowTextures, gpTextureManager != nullptr);

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

	if (gpInstanceManager != nullptr) [[likely]]
	{
		PollSetting<float>(gTerrainElevationTextureMultiplier, "TerrainElevationTexture multiplier", DestroyType::kPipelines, DestroyFlags::kTerrainElevation);

		auto [fSmokeTrailPower, fSmokeTrailPowerPrevious, bSmokeTrailPowerChanged] = gSmokeTrailPower.Changed<float>();
		auto [fSmokeTrailAlpha, fSmokeTrailAlphaPrevious, bSmokeTrailAlphaChanged] = gSmokeTrailAlpha.Changed<float>();
		auto [fSmokeSimulationPixels, fPreviousSmokeSimulationPixels, bSmokeSimulationPixelsChanged] = gSmokeSimulationPixels.Changed<float>();
		auto [fSmokeSimulationArea, fPreviousSmokeSimulationArea, bSmokeSimulationAreaChanged] = gSmokeSimulationArea.Changed<float>();
		if (bSmokeTrailPowerChanged || bSmokeTrailAlphaChanged || bSmokeSimulationPixelsChanged || bSmokeSimulationAreaChanged) [[unlikely]]
		{
			LOG(kGraphics, kDebug, "SmokeSimulationPixels: {} -> {} ({} -> {})", common::Wb(fPreviousSmokeSimulationPixels, 3), common::Wb(fSmokeSimulationPixels, 3), common::Wb(gSmokeSimulationPixels.Get(), 3), common::Wb(SmokeSimulationPixels(), 3));
			LOG(kGraphics, kDebug, "SmokeSimulationArea: {} -> {}", common::Wb(fPreviousSmokeSimulationArea, 3), common::Wb(fSmokeSimulationArea, 3));

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
			mOldVkSwapchainKHR = mpSwapchainManager->ReleaseHandleForRecreation();
		}
		mpSwapchainManager.reset();
		if constexpr (kbDebugInput)
		{
			if (game::gpGame != nullptr)
			{
				game::SaveTweaksSettings();
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
