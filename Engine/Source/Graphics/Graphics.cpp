#if defined(BT_CLIENT)

#include "Graphics.h"

#include "Game.h"
#include "Profile/ProfileManager.h"

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
		Log("FullDetail: {} x {}", iX, iY);
		siX = iX;
		siY = iY;
	}

	return std::make_tuple(iX, iY);
}

float SmokeSimulationPixels()
{
	static constexpr float kfReferencePixels = 3840.0f;
	float fPixels = static_cast<float>(gpGraphics->mFramebufferExtent2D.width);
	return (fPixels / kfReferencePixels) * 8192.0f * gSmokeSimulationPixels.Get();
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
			Log("Active display device \"{}\" has frequency of {} Hz", displayDevice.DeviceName, devmodea.dmDisplayFrequency);
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

	// Process pending texture loads after fence wait when it's safe to update GPU resources
	gpTextureManager->ProcessPendingTextures(iCommandBuffer);

	// Update VMA frame index for memory budget tracking
	if (gpDeviceManager->mbMemoryBudgetAvailable)
	{
		vmaSetCurrentFrameIndex(gpDeviceManager->mpAllocator, static_cast<uint32_t>(muiFrameCounter++));
	}

	if (rCommandBuffers.mFlags & CommandBufferFlags::kExecuted)
	{
		gpProfileManager->GpuRead(iCommandBuffer, kGpuTimerGlobal, kGpuTimerCount);
	}

	gpProfileManager->CpuStart(kCpuTimerRenderGlobal);
	RenderFrameGlobal(iCommandBuffer, fCurrentTime, game::gpGame->TickCounter());
	gpParticleManager->RenderGlobal(iCommandBuffer);
	gpProfileManager->CpuStop(kCpuTimerRenderGlobal, false);

	gpCommandBufferManager->SubmitGlobalCommandBuffer(iCommandBuffer);
}

void Graphics::RenderMainPresentAcquire(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord)
{
	gpProfileManager->CpuStart(kCpuTimerRenderMain);
	RenderFrameMain(iCommandBuffer, rRenderInterpolates, rActiveCoords, cameraCoord);
	gpProfileManager->CpuStop(kCpuTimerRenderMain, false);

	gpCommandBufferManager->SubmitMainCommandBuffer(iCommandBuffer, false);

	gpCommandBufferManager->SubmitUiCommandBuffer(iCommandBuffer);

	gpSwapchainManager->Present(iCommandBuffer);

	// Signal upload thread to process one upload iteration
	gpTextureUploadManager->mFrameSignal.release();

	// Renders per second
	mRendersInTheLastSecond.Set();

	gpProfileManager->UpdateProfileText();

	if constexpr (kbEnableRenderThread)
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
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	Refresh();
	Destroy();

	if (mpInstanceManager == nullptr) { mpInstanceManager = std::make_unique<InstanceManager>(mHinstance, mHwnd); }
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
	if constexpr (kbEnableProfiling)
	{
		gpProfileManager->Create();
	}
	bool bRecordCommandBuffers = false;
	if (mpCommandBufferManager == nullptr)
	{
		mpCommandBufferManager = std::make_unique<CommandBufferManager>();
		bRecordCommandBuffers = true;
	}
	if (mpBufferManager == nullptr) { mpBufferManager = std::make_unique<BufferManager>(); }
	else if (bSwapchainRecreated) { gpBufferManager->CreateSwapchainDependentBuffers(); }
	if (mpIslands == nullptr) { mpIslands = std::make_unique<Islands>(); }
	if (mpTextureManager == nullptr) { mpTextureManager = std::make_unique<TextureManager>(); }
	else if (bSwapchainRecreated) { gpTextureManager->CreateScreenDependentResources(); }
	if (mpTextManager == nullptr) { mpTextManager = std::make_unique<TextManager>(); }
	if (mpPipelineManager == nullptr)
	{
		mpPipelineManager = std::make_unique<PipelineManager>();
	}
	if (mpParticleManager == nullptr) { mpParticleManager = std::make_unique<ParticleManager>(); }

	if (mpImGuiManager == nullptr) { mpImGuiManager = std::make_unique<ImGuiManager>(mHwnd); }

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
		Log("Multisampling: {} -> {}", bPreviousMultisampling, bMultisampling);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gpInstanceManager != nullptr && gSampleCount.Get<VkSampleCountFlagBits>() > gpInstanceManager->meMaxMultisampleCount)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(gpInstanceManager->meMaxMultisampleCount);
	}

	auto [eSampleCount, ePreviousSampleCount, bSampleCountChanged] = gSampleCount.Changed<VkSampleCountFlagBits>();
	if (bSampleCountChanged) [[unlikely]]
	{
		Log("Sample count: {} -> {}", static_cast<int64_t>(ePreviousSampleCount), static_cast<int64_t>(eSampleCount));
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [ePresentMode, ePreviousPresentMode, bPresentModeChanged] = gPresentMode.Changed<VkPresentModeKHR>();
	if (bPresentModeChanged) [[unlikely]]
	{
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		auto pcPreviousPresentMode = gEnumToString.Convert(ePreviousPresentMode, rWorkbuffer);
		auto pcPresentMode = gEnumToString.Convert(ePresentMode, rWorkbuffer);
		Log("{} -> {}", pcPreviousPresentMode, pcPresentMode);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gWantedFramebufferExtent2D.width != mFramebufferExtent2D.width || gWantedFramebufferExtent2D.height != mFramebufferExtent2D.height) [[unlikely]]
	{
		Log("{} x {} -> {} x {}", mFramebufferExtent2D.width, mFramebufferExtent2D.height, gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
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
		Log("Anisotropy: {} -> {}", bPreviousAnisotropy, bAnisotropy);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [fMaxAnisotropy, fPreviousMaxAnisotropy, bMaxAnisotropyChanged] = gMaxAnisotropy.Changed<float>();
	if (bMaxAnisotropyChanged) [[unlikely]]
	{
		Log("Max anisotropy: {} -> {}", fPreviousMaxAnisotropy, fMaxAnisotropy);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [bSampleShading, bPreviousSampleShading, bSampleShadingChanged] = gSampleShading.Changed<bool>();
	if (bSampleShadingChanged) [[unlikely]]
	{
		Log("Sample shading: {} -> {}", bPreviousSampleShading, bSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMinSampleShading, fPreviousMinSampleShading, bMinSampleShadingChanged] = gMinSampleShading.Changed<float>();
	if (bMinSampleShadingChanged) [[unlikely]]
	{
		Log("Min sample shading: {} -> {}", fPreviousMinSampleShading, fMinSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMipLodBias, fPreviousMipLodBias, bMipLodBiasChanged] = gMipLodBias.Changed<float>();
	if (bMipLodBiasChanged) [[unlikely]]
	{
		Log("Mip lod bias: {} -> {}", fPreviousMipLodBias, fMipLodBias);
		meDestroyType = std::max(DestroyType::kSamplers, meDestroyType);
	}

	auto [bWireframe, bPreviousWireframe, bWireframeChanged] = gWireframe.Changed<bool>();
	if (bWireframeChanged) [[unlikely]]
	{
		Log("Wireframe: {} -> {}", bPreviousWireframe, bWireframe);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [bDebugTexture, bPreviousDebugTexture, bDebugTextureChanged] = gDebugTexture.Changed<bool>();
	if (bDebugTextureChanged) [[unlikely]]
	{
		meDestroyType = std::max(DestroyType::kCommandBuffers, meDestroyType);
	}

	auto [fWorldDetail, fPreviousWorldDetail, bWorldDetailChanged] = gWorldDetail.Changed<float>();
	if (bWorldDetailChanged && gpBufferManager != nullptr) [[unlikely]]
	{
		Log("World detail: {} -> {}", fPreviousWorldDetail, fWorldDetail);

		mDestroyFlags.Set({DestroyFlags::kTerrainMesh, DestroyFlags::kShadowTextures, DestroyFlags::kObjectShadows, DestroyFlags::kLightingTextures, DestroyFlags::kWaterMesh});

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fLightingBlurFirstDivisor, fLightingBlurFirstDivisorPrevious, bLightingBlurFirstDivisorChanged] = gLightingBlurFirstDivisor.Changed<float>();
	auto [fLightingBlurDivisor, fLightingBlurDivisorPrevious, bLightingBlurDivisorChanged] = gLightingBlurDivisor.Changed<float>();
	if (bLightingBlurFirstDivisorChanged || bLightingBlurDivisorChanged)
	{
		meDestroyType = std::max(DestroyType::kCommandBuffers, meDestroyType);
	}

	auto [fLightingMultiplier, fLightingMultiplierPrevious, bLightingMultiplierChanged] = gLightingTextureMultiplier.Changed<float>();
	auto [fLightingBlurDownscale, fLightingBlurDownscalePrevious, bLightingBlurDownscaleChanged] = gLightingBlurDownscale.Changed<float>();
	auto [fLightingCombineIndex, fLightingCombineIndexPrevious, bLightingCombineIndexChanged] = gLightingCombineIndex.Changed<float>();
	if ((bLightingMultiplierChanged || bLightingBlurDownscaleChanged || bLightingCombineIndexChanged) && gpTextureManager != nullptr) [[unlikely]]
	{
		mDestroyFlags.Set(DestroyFlags::kLightingTextures);

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
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
		auto [fTerrainElevationTextureMultiplier, fPreviousTerrainElevationTextureMultiplier, bTerrainElevationTextureMultiplierChanged] = gTerrainElevationTextureMultiplier.Changed<float>();
		if (bTerrainElevationTextureMultiplierChanged) [[unlikely]]
		{
			Log("TerrainElevationTexture multiplier: {} -> {}", fPreviousTerrainElevationTextureMultiplier, fTerrainElevationTextureMultiplier);

			mDestroyFlags.Set(DestroyFlags::kTerrainElevation);

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainColorTextureMultiplier, fPreviousTerrainColorTextureMultiplier, bTerrainColorTextureMultiplierChanged] = gTerrainColorTextureMultiplier.Changed<float>();
		if (bTerrainColorTextureMultiplierChanged) [[unlikely]]
		{
			Log("TerrainColorTexture multiplier: {} -> {}", fPreviousTerrainColorTextureMultiplier, fTerrainColorTextureMultiplier);

			mDestroyFlags.Set(DestroyFlags::kTerrainColor);

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainNormalTextureMultiplier, fPreviousTerrainNormalTextureMultiplier, bTerrainNormalTextureMultiplierChanged] = gTerrainNormalTextureMultiplier.Changed<float>();
		if (bTerrainNormalTextureMultiplierChanged) [[unlikely]]
		{
			Log("TerrainNormalTexture multiplier: {} -> {}", fPreviousTerrainNormalTextureMultiplier, fTerrainNormalTextureMultiplier);

			mDestroyFlags.Set(DestroyFlags::kTerrainNormal);

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainAmbientOcclusionTextureMultiplier, fPreviousTerrainAmbientOcclusionTextureMultiplier, bTerrainAmbientOcclusionTextureMultiplierChanged] = gTerrainAmbientOcclusionTextureMultiplier.Changed<float>();
		if (bTerrainAmbientOcclusionTextureMultiplierChanged) [[unlikely]]
		{
			Log("TerrainAmbientOcclusionTexture multiplier: {} -> {}", fPreviousTerrainAmbientOcclusionTextureMultiplier, fTerrainAmbientOcclusionTextureMultiplier);

			mDestroyFlags.Set(DestroyFlags::kTerrainAO);

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fSmokeTrailPower, fSmokeTrailPowerPrevious, bSmokeTrailPowerChanged] = gSmokeTrailPower.Changed<float>();
		auto [fSmokeTrailAlpha, fSmokeTrailAlphaPrevious, bSmokeTrailAlphaChanged] = gSmokeTrailAlpha.Changed<float>();
		auto [fSmokeSimulationPixels, fPreviousSmokeSimulationPixels, bSmokeSimulationPixelsChanged] = gSmokeSimulationPixels.Changed<float>();
		auto [fSmokeSimulationArea, fPreviousSmokeSimulationArea, bSmokeSimulationAreaChanged] = gSmokeSimulationArea.Changed<float>();
		if (bSmokeTrailPowerChanged || bSmokeTrailAlphaChanged || bSmokeSimulationPixelsChanged || bSmokeSimulationAreaChanged) [[unlikely]]
		{
			Log("SmokeSimulationPixels: {} -> {} ({} -> {})", fPreviousSmokeSimulationPixels, fSmokeSimulationPixels, gSmokeSimulationPixels.Get(), SmokeSimulationPixels());
			Log("SmokeSimulationArea: {} -> {}", fPreviousSmokeSimulationArea, fSmokeSimulationArea);

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

	if (mDestroyFlags & DestroyFlags::kTerrainMesh)
	{
		if (gpBufferManager != nullptr)
		{
			gpBufferManager->CreateTerrainMesh();
		}
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
	}

	if (gpTextureManager != nullptr)
	{
		struct TerrainTextureRecreateDesc
		{
			DestroyFlags eFlag;
			Wrapper& rMultiplierCvar;
			Texture& rTexture;
		};

		TerrainTextureRecreateDesc pTerrainDescs[]
		{
			{DestroyFlags::kTerrainElevation, gTerrainElevationTextureMultiplier, gpTextureManager->mRenderTargetTextures.mTerrainElevationTexture},
			{DestroyFlags::kTerrainColor, gTerrainColorTextureMultiplier, gpTextureManager->mRenderTargetTextures.mTerrainColorTexture},
			{DestroyFlags::kTerrainNormal, gTerrainNormalTextureMultiplier, gpTextureManager->mRenderTargetTextures.mTerrainNormalTexture},
			{DestroyFlags::kTerrainAO, gTerrainAmbientOcclusionTextureMultiplier, gpTextureManager->mRenderTargetTextures.mTerrainAmbientOcclusionTexture},
		};

		for (const TerrainTextureRecreateDesc& rDesc : pTerrainDescs)
		{
			if (mDestroyFlags & rDesc.eFlag)
			{
				auto [iX, iY] = gpTextureManager->DetailTextureSize(rDesc.rMultiplierCvar.Get());
				rDesc.rTexture.mInfo.extent.width = static_cast<uint32_t>(iX);
				rDesc.rTexture.mInfo.extent.height = static_cast<uint32_t>(iY);
				rDesc.rTexture.ReCreate();
			}
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

	Log("Graphics::Destroy() {}", static_cast<int64_t>(meDestroyType));

	if (gpDeviceManager != nullptr)
	{
		// This idle wait only occurs:
		// 1. On app shutdown
		// 2. On Vulkan error (to re-create Surface/Swapchain)
		// 3. When the user changes graphics settings
		vkDeviceWaitIdle(gpDeviceManager->mVkDevice);
	}

	// Save flags before RecreateResources() clears them (needed for selective pipeline recreation)
	DestroyFlags_t savedFlags = mDestroyFlags;
	bool bSelectiveRecreation = meDestroyType == DestroyType::kPipelines && !savedFlags.Empty() && !(savedFlags & DestroyFlags::kObjectShadows);

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
			if (meDestroyType < DestroyType::kPipelines || bSelectiveRecreation)
			{
				gpTextureManager->mTextureDescriptors.RewriteSamplerDescriptors();
			}
		}
		if (gpImGuiManager != nullptr)
		{
			gpImGuiManager->RecreateSamplerDependencies();
		}
	}

	if (meDestroyType >= DestroyType::kCommandBuffers)
	{
		mpCommandBufferManager.reset();
	}

	if (meDestroyType >= DestroyType::kPipelines)
	{
		// Selectively recreate only affected pipeline groups when possible
		if (bSelectiveRecreation)
		{
			gpPipelineManager->RecreatePipelineGroups(savedFlags);
		}
		else
		{
			mpPipelineManager.reset();
		}
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
		if constexpr (kbEnableProfiling)
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
		mpImGuiManager.reset();
	}

	if (meDestroyType >= DestroyType::kSurface)
	{
		mpIslands.reset();
		gpTextureUploadManager->DestroyTransferResources();
		// Reset lazy-loaded texture chunk states so they reload after device recreation
		gpFileManager->ResetTextureChunkStates();
		mpDeviceManager.reset();
		mpInstanceManager.reset();
	}

	mDestroyFlags = DestroyFlags_t {};
	meDestroyType = DestroyType::kNone;

	return true;
}

} // namespace engine

#endif // BT_CLIENT
