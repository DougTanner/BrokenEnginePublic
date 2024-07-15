#include "Graphics.h"

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Profile/ProfileManager.h"
#include "Ui/Wrapper.h"

#include "Game.h"

using namespace DirectX;

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
		LOG("FullDetail: {} x {}", iX, iY);
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

Graphics::Graphics(HINSTANCE hinstance, HWND hwnd)
: mHinstance(hinstance)
, mHwnd(hwnd)
{
	gpGraphics = this;

	Create();

	gpSwapchainManager->AcquireNextImage();
}

Graphics::~Graphics()
{
	meDestroyType = DestroyType::kSurface;
	Destroy();

	gpGraphics = nullptr;
}

void Graphics::RenderGlobal(const game::Frame& __restrict rFrame)
{
	int64_t iCommandBuffer = gpCommandBufferManager->CommandBufferIndex(gpSwapchainManager->miFramebufferIndex);

	CommandBuffers& rCommandBuffers = gpCommandBufferManager->mPerFramebufferCommandBuffers.at(gpSwapchainManager->miFramebufferIndex);
	VkResult vkResult = vkGetFenceStatus(gpDeviceManager->mVkDevice, rCommandBuffers.mpVkFences[rCommandBuffers.miCurrentIndex]);
	if (vkResult == VK_NOT_READY)
	{
		SCOPED_CPU_PROFILE(kCpuTimerWaitFence);
		CHECK_VK(vkWaitForFences(gpDeviceManager->mVkDevice, 1, &rCommandBuffers.mpVkFences[rCommandBuffers.miCurrentIndex], VK_TRUE, kFenceTimeoutNs.count()));
	}

	gpCommandBufferManager->RecordCommandBuffer(gpSwapchainManager->miFramebufferIndex);

	if (rCommandBuffers.mpbExecuted[rCommandBuffers.miCurrentIndex])
	{
		GPU_PROFILE_READ(iCommandBuffer, kGpuTimerGlobal, kGpuTimerCount);
	}

	CPU_PROFILE_START(kCpuTimerRenderGlobal);
	CalculateMatricesAndVisibleArea(rFrame, true);
	RenderFrameGlobal(iCommandBuffer, rFrame);
	gpParticleManager->RenderGlobal(iCommandBuffer, rFrame);
	CPU_PROFILE_STOP(kCpuTimerRenderGlobal);

	gpCommandBufferManager->SubmitGlobalCommandBuffer();
}

void Graphics::RenderMainImagePresentAcquire(const game::Frame& __restrict rFrame)
{
	int64_t iCommandBuffer = gpCommandBufferManager->CommandBufferIndex(gpSwapchainManager->miFramebufferIndex);

	{
		// Copy staged uniform buffers
		CPU_PROFILE_START(kCpuTimerRenderMain);
		CalculateMatricesAndVisibleArea(rFrame, false);
		RenderFrameMain(iCommandBuffer, rFrame);
		gpUiManager->RenderMain(iCommandBuffer);
		gpTextManager->RenderMain(iCommandBuffer);
		CPU_PROFILE_STOP(kCpuTimerRenderMain);

		gpCommandBufferManager->SubmitMainCommandBuffer();
	}

	{
		gpCommandBufferManager->SubmitImageCommandBuffer();

		gpSwapchainManager->Present();

		// Renders per second
		mRendersInTheLastSecond.Set();

		UPDATE_PROFILE_TEXT();

	#if defined(ENABLE_RENDER_THREAD)
		CPU_PROFILE_START(kCpuTimerWaitPresentFuture);
		if (gpSwapchainManager->mPresent.valid())
		{
			gpSwapchainManager->mPresent.get();
		}
		CPU_PROFILE_STOP(kCpuTimerWaitPresentFuture);
	#endif

		Create();

		gpSwapchainManager->AcquireNextImage();
		CPU_PROFILE_START(kCpuTimerAcquireToGlobal);
	}
}

void Graphics::Create()
{
	Refresh();
	bool bDestroyed = Destroy();

	if (mpInstanceManager == nullptr) { mpInstanceManager = std::make_unique<InstanceManager>(mHinstance, mHwnd); }
	if (mpDeviceManager == nullptr) { mpDeviceManager = std::make_unique<DeviceManager>(); }
	if (mpShaderManager == nullptr) { mpShaderManager = std::make_unique<ShaderManager>(); }
	if (mpSwapchainManager == nullptr) { mpSwapchainManager = std::make_unique<SwapchainManager>(); }
	#if defined(ENABLE_PROFILING)
	gpProfileManager->Create();
	#endif
	if (mpCommandBufferManager == nullptr) { mpCommandBufferManager = std::make_unique<CommandBufferManager>(); }
	if (mpBufferManager == nullptr) { mpBufferManager = std::make_unique<BufferManager>(); }
	if (mpIslands == nullptr) { mpIslands = std::make_unique<Islands>(); }
	if (mpTextureManager == nullptr) { mpTextureManager = std::make_unique<TextureManager>(); }
	if (mpTextManager == nullptr) { mpTextManager = std::make_unique<TextManager>(); }
	if (mpUiManager == nullptr) { mpUiManager = std::make_unique<UiManager>(); }
	{
		SCOPED_BOOT_TIMER(kBootTimerBuildGlobalHeightmap);
		mpIslands->BuildGlobalHeightmap();
	}
	if (mpPipelineManager == nullptr) { mpPipelineManager = std::make_unique<PipelineManager>(); }
	if (mpParticleManager == nullptr) { mpParticleManager = std::make_unique<ParticleManager>(); }

	if (bDestroyed && game::gpGame != nullptr)
	{
		game::gpGame->ResetRealTime();
	}
}

void Graphics::Refresh()
{
	auto [bMultisampling, bPreviousMultisampling, bMultisamplingChanged] = gMultisampling.Changed<bool>();
	if (bMultisamplingChanged) [[unlikely]]
	{
		LOG("Multisampling: {} -> {}", bPreviousMultisampling, bMultisampling);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gpInstanceManager != nullptr && gSampleCount.Get<VkSampleCountFlagBits>() > gpInstanceManager->meMaxMultisampleCount)
	{
		gSampleCount.Set<VkSampleCountFlagBits>(gpInstanceManager->meMaxMultisampleCount);
	}

	auto [eSampleCount, ePreviousSampleCount, bSampleCountChanged] = gSampleCount.Changed<VkSampleCountFlagBits>();
	if (bSampleCountChanged) [[unlikely]]
	{
		LOG("Sample count: {} -> {}", static_cast<int64_t>(ePreviousSampleCount), static_cast<int64_t>(eSampleCount));
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [ePresentMode, ePreviousPresentMode, bPresentModeChanged] = gPresentMode.Changed<VkPresentModeKHR>();
	if (bPresentModeChanged) [[unlikely]]
	{
		LOG("{} -> {}", gEnumToString.mVkPresentModeKHRMap.at(ePreviousPresentMode), gEnumToString.mVkPresentModeKHRMap.at(ePresentMode));
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	if (gWantedFramebufferExtent2D.width != mFramebufferExtent2D.width || gWantedFramebufferExtent2D.height != mFramebufferExtent2D.height) [[unlikely]]
	{
		LOG("{} x {} -> {} x {}", mFramebufferExtent2D.width, mFramebufferExtent2D.height, gWantedFramebufferExtent2D.width, gWantedFramebufferExtent2D.height);
		mFramebufferExtent2D = gWantedFramebufferExtent2D;
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [bAnisotropy, bPreviousAnisotropy, bAnisotropyChanged] = gAnisotropy.Changed<bool>();
	if (bAnisotropyChanged) [[unlikely]]
	{
		LOG("Anisotropy: {} -> {}", bPreviousAnisotropy, bAnisotropy);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);

		if (gpTextManager != nullptr && gpTextureManager != nullptr)
		{
			gpTextureManager->DestroySamplers();
			gpTextureManager->CreateSamplers();
		}
	}

	auto [fMaxAnisotropy, fPeviousMaxAnisotropy, bMaxAnisotropyChanged] = gMaxAnisotropy.Changed<float>();
	if (bMaxAnisotropyChanged) [[unlikely]]
	{
		LOG("Max anisotropy: {} -> {}", fPeviousMaxAnisotropy, fMaxAnisotropy);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);

		if (gpTextManager != nullptr && gpTextureManager != nullptr)
		{
			gpTextureManager->DestroySamplers();
			gpTextureManager->CreateSamplers();
		}
	}

	auto [bSampleShading, bPreviousSampleShading, bSampleShadingChanged] = gSampleShading.Changed<bool>();
	if (bSampleShadingChanged) [[unlikely]]
	{
		LOG("Sample shading: {} -> {}", bPreviousSampleShading, bSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMinSampleShading, fPeviousMinSampleShading, bMinSampleShadingChanged] = gMinSampleShading.Changed<float>();
	if (bMinSampleShadingChanged) [[unlikely]]
	{
		LOG("Min sample shading: {} -> {}", fPeviousMinSampleShading, fMinSampleShading);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fMipLodBias, fPeviousMipLodBias, bMipLodBiasChanged] = gMipLodBias.Changed<float>();
	if (bMipLodBiasChanged) [[unlikely]]
	{
		LOG("Mip lod bias: {} -> {}", fPeviousMipLodBias, fMipLodBias);
		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);

		if (gpTextManager != nullptr && gpTextureManager != nullptr)
		{
			gpTextureManager->DestroySamplers();
			gpTextureManager->CreateSamplers();
		}
	}

	auto [bWireframe, bPreviousWireframe, bWireframeChanged] = gWireframe.Changed<bool>();
	if (bWireframeChanged) [[unlikely]]
	{
		LOG("Wireframe: {} -> {}", bPreviousWireframe, bWireframe);
		meDestroyType = std::max(DestroyType::kSwapchain, meDestroyType);
	}

	auto [fWorldDetail, fPeviousWorldDetail, bWorldDetailChanged] = gWorldDetail.Changed<float>();
	if (bWorldDetailChanged && gpBufferManager != nullptr) [[unlikely]]
	{
		LOG("World detail: {} -> {}", fPeviousWorldDetail, fWorldDetail);

		gpBufferManager->CreateTerrainMesh();

		gpTextureManager->CreateShadowTextures();
		gpPipelineManager->CreateShadowPipelines();

		gpTextureManager->CreateObjectShadowsTextures();
		game::gpGltfPipelines->CreateGltfShadowPipelines();

		gpPipelineManager->CreateLightingShadowDependantPipelines();

		gpBufferManager->CreateWaterMesh();

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
		gpTextureManager->CreateLightingTextures();
		gpPipelineManager->CreateLightingPipelines();
		gpPipelineManager->CreateLightingShadowDependantPipelines();

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	auto [fObjectShadowsRenderMultiplier, fObjectShadowsRenderMultiplierPrevious, bObjectShadowsRenderMultiplierChanged] = gObjectShadowsRenderMultiplier.Changed<float>();
	auto [fObjectShadowsBlurMultiplier, fObjectShadowsBlurMultiplierPrevious, bObjectShadowsBlurMultiplierChanged] = gObjectShadowsBlurMultiplier.Changed<float>();
	if ((bObjectShadowsRenderMultiplierChanged || bObjectShadowsBlurMultiplierChanged) && gpTextureManager != nullptr) [[unlikely]]
	{
		gpTextureManager->CreateObjectShadowsTextures();
		game::gpGltfPipelines->CreateGltfShadowPipelines();
		gpPipelineManager->CreateLightingShadowDependantPipelines();

		meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
	}

	if (gpInstanceManager != nullptr) [[likely]]
	{
		auto [fTerrainElevationTextureMultiplier, fPeviousTerrainElevationTextureMultiplier, bTerrainElevationTextureMultiplierChanged] = gTerrainElevationTextureMultiplier.Changed<float>();
		if (bTerrainElevationTextureMultiplierChanged) [[unlikely]]
		{
			LOG("TerrainElevationTexture multiplier: {} -> {}", fPeviousTerrainElevationTextureMultiplier, fTerrainElevationTextureMultiplier);

			auto [iX, iY] = gpTextureManager->DetailTextureSize(fTerrainElevationTextureMultiplier);
			gpTextureManager->mTerrainElevationTexture.mInfo.extent.width = static_cast<uint32_t>(iX);
			gpTextureManager->mTerrainElevationTexture.mInfo.extent.height = static_cast<uint32_t>(iY);
			gpTextureManager->mTerrainElevationTexture.ReCreate();

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainColorTextureMultiplier, fPeviousTerrainColorTextureMultiplier, bTerrainColorTextureMultiplierChanged] = gTerrainColorTextureMultiplier.Changed<float>();
		if (bTerrainColorTextureMultiplierChanged) [[unlikely]]
		{
			LOG("TerrainColorTexture multiplier: {} -> {}", fPeviousTerrainColorTextureMultiplier, fTerrainColorTextureMultiplier);

			auto [iX, iY] = gpTextureManager->DetailTextureSize(fTerrainColorTextureMultiplier);
			gpTextureManager->mTerrainColorTexture.mInfo.extent.width = static_cast<uint32_t>(iX);
			gpTextureManager->mTerrainColorTexture.mInfo.extent.height = static_cast<uint32_t>(iY);
			gpTextureManager->mTerrainColorTexture.ReCreate();

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainNormalTextureMultiplier, fPeviousTerrainNormalTextureMultiplier, bTerrainNormalTextureMultiplierChanged] = gTerrainNormalTextureMultiplier.Changed<float>();
		if (bTerrainNormalTextureMultiplierChanged) [[unlikely]]
		{
			LOG("TerrainNormalTexture multiplier: {} -> {}", fPeviousTerrainNormalTextureMultiplier, fTerrainNormalTextureMultiplier);

			auto [iX, iY] = gpTextureManager->DetailTextureSize(fTerrainNormalTextureMultiplier);
			gpTextureManager->mTerrainNormalTexture.mInfo.extent.width = static_cast<uint32_t>(iX);
			gpTextureManager->mTerrainNormalTexture.mInfo.extent.height = static_cast<uint32_t>(iY);
			gpTextureManager->mTerrainNormalTexture.ReCreate();

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fTerrainAmbientOcclusionTextureMultiplier, fPeviousTerrainAmbientOcclusionTextureMultiplier, bTerrainAmbientOcclusionTextureMultiplierChanged] = gTerrainAmbientOcclusionTextureMultiplier.Changed<float>();
		if (bTerrainAmbientOcclusionTextureMultiplierChanged) [[unlikely]]
		{
			LOG("TerrainAmbientOcclusionTexture multiplier: {} -> {}", fPeviousTerrainAmbientOcclusionTextureMultiplier, fTerrainAmbientOcclusionTextureMultiplier);

			auto [iX, iY] = gpTextureManager->DetailTextureSize(fTerrainAmbientOcclusionTextureMultiplier);
			gpTextureManager->mTerrainAmbientOcclusionTexture.mInfo.extent.width = static_cast<uint32_t>(iX);
			gpTextureManager->mTerrainAmbientOcclusionTexture.mInfo.extent.height = static_cast<uint32_t>(iY);
			gpTextureManager->mTerrainAmbientOcclusionTexture.ReCreate();

			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}

		auto [fSmokeTrailPower, fSmokeTrailPowerPrevious, bSmokeTrailPowerChanged] = gSmokeTrailPower.Changed<float>();
		auto [fSmokeTrailAlpha, fSmokeTrailAlphaPrevious, bSmokeTrailAlphaChanged] = gSmokeTrailAlpha.Changed<float>();
		auto [fSmokeSimulationPixels, fPeviousSmokeSimulationPixels, bSmokeSimulationPixelsChanged] = gSmokeSimulationPixels.Changed<float>();
		auto [fSmokeSimulationArea, fPeviousSmokeSimulationArea, bSmokeSimulationAreaChanged] = gSmokeSimulationArea.Changed<float>();
		if (bSmokeTrailPowerChanged || bSmokeTrailAlphaChanged || bSmokeSimulationPixelsChanged || bSmokeSimulationAreaChanged) [[unlikely]]
		{
			LOG("SmokeSimulationPixels: {} -> {} ({} -> {})", fPeviousSmokeSimulationPixels, fSmokeSimulationPixels, gSmokeSimulationPixels.Get(), SmokeSimulationPixels());
			LOG("SmokeSimulationArea: {} -> {}", fPeviousSmokeSimulationArea, fSmokeSimulationArea, gSmokeSimulationArea.Get());

			gpTextureManager->CreateSmokeTextures();
			meDestroyType = std::max(DestroyType::kPipelines, meDestroyType);
		}
	}
}

bool Graphics::Destroy()
{
	if (meDestroyType == DestroyType::kNone)
	{
		return false;
	}

	LOG("Graphics::Destroy() {}", static_cast<int64_t>(meDestroyType));

	if (game::gpGame != nullptr)
	{
		game::gpGame->mAverageDelta.miCount = 0;
	}

	if (gpDeviceManager != nullptr)
	{
		vkDeviceWaitIdle(gpDeviceManager->mVkDevice);
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
		mpTextureManager.reset();
		mpBufferManager.reset();
	#if defined(ENABLE_PROFILING)
		gpProfileManager->Destroy();
	#endif
		mpSwapchainManager.reset();
	}

	if (meDestroyType >= DestroyType::kSurface)
	{
		mpUiManager.reset();
		mpIslands.reset();
		mpShaderManager.reset();
		mpDeviceManager.reset();
		mpInstanceManager.reset();
	}

	meDestroyType = DestroyType::kNone;

	return true;
}

} // namespace engine
