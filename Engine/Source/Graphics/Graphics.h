#pragma once

#if defined(BT_CLIENT)

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

class BufferManager;
class CommandBufferManager;
class DeviceManager;
class ImGuiManager;
class InstanceManager;
class Islands;
class ParticleManager;
class PipelineManager;
class SwapchainManager;
class TextureManager;
class Wrapper;

enum DestroyType
{
	kNone,
	kCommandBuffers,
	kSamplers,
	kPipelines,
	kSwapchain,
	kSurface,
};

enum class DestroyFlags : uint16_t
{
	kShadowTextures     = 0x0004,
	kObjectShadows      = 0x0008,
	kLightingTextures   = 0x0010,
	kWaterMesh          = 0x0020,
	kTerrainElevation   = 0x0040,
	kSmokeTextures      = 0x0400,
};
using DestroyFlags_t = common::Flags<DestroyFlags>;

inline VkExtent2D gWantedFramebufferExtent2D {};

std::tuple<int64_t, int64_t> FullDetail();
std::tuple<int64_t, int64_t> WaterFullDetail();
float SmokeSimulationPixels();
float SmokeSimulationPixelsY();

// Agent/dev screenshot request queued into Graphics::mScreenshotRequest; consumed once at the capture site in
// RenderMainPresentAcquire. Empty path -> default %TEMP%\Screenshots\agent_{N}.{jpg|png}. iMaxWidth <= 0 disables
// downscale; the image is downscaled preserving aspect when wider than iMaxWidth.
struct ScreenshotRequest
{
	std::filesystem::path path;
	int64_t iMaxWidth = 1568;
	bool bPng = false;
	int64_t iQuality = 80;
	// Set only by the agent screenshot handler: the async save publishes its result JSON to the capture-result slot
	// (consumed by the deferred-response poll). False for F9 dev saves so they never publish into an agent response.
	bool bPublishResult = false;
	// Capture token minted and made active by ResetCaptureResult(); the async save publishes only while it remains
	// active, so an overlapping abandoned encoder cannot replace a newer request's result. 0 for dev saves.
	uint64_t uiCaptureToken = 0;
};

// Agent render-target dump request queued into Graphics::mDumpRenderTargetRequest; consumed at the same capture
// site. name selects a RenderTargetTextures member (validated at request time); iIndex/iChannel index the array
// members. Empty path -> default %TEMP%\Screenshots\dump_{name}_{N}. bRaw additionally writes the raw texel .bin.
struct DumpRenderTargetRequest
{
	std::string name;
	int64_t iIndex = 0;
	int64_t iChannel = 0;
	std::filesystem::path path;
	bool bRaw = false;
	// As ScreenshotRequest::bPublishResult — set only by the agent dump handler so the encode result reaches the
	// deferred-response poll and no non-agent save is consumed as the response to a concurrent agent capture.
	bool bPublishResult = false;
	// As ScreenshotRequest::uiCaptureToken — publication succeeds only while this request remains active, so an
	// overlapping abandoned encoder cannot replace a newer request's result. 0 for non-agent dumps.
	uint64_t uiCaptureToken = 0;
};

class Graphics
{
public:

	Graphics(HINSTANCE hinstance, HWND hwnd);
	~Graphics();

	Graphics() = delete;

	void RenderGlobal(float fCurrentTime);
	// Wait on all in-flight per-framebuffer fences (not just the current one). Used to quiesce the
	// graphics queue before island eviction/restoration frees images / rewrites descriptors.
	void WaitAllFramebufferFencesIdle();
	void RenderMainPresentAcquire(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord);
	void Create();
	void Refresh();
	bool Destroy();
	void RecreateResources();
	// True once the live framebuffer extent matches the wanted extent AND no swapchain recreate is deferred — i.e.
	// a live swapchain actually backs the wanted extent. Extent equality alone is a false positive while deferred
	// (Refresh copies the wanted extent at trigger time before the defer gate returns early).
	bool ExtentSettled() const;

	HINSTANCE mHinstance = nullptr;
	HWND mHwnd = nullptr;
	int64_t miMonitorRefreshRate = 60;
	uint64_t muiFrameCounter = 0;
	VkExtent2D mFramebufferExtent2D = gWantedFramebufferExtent2D;

	DestroyType meDestroyType = DestroyType::kNone;
	DestroyFlags_t mDestroyFlags;
	VkSwapchainKHR mOldVkSwapchainKHR = VK_NULL_HANDLE;
	// Latch so a swapchain recreate deferred while the window is off-screen/minimized logs once per transition, not per frame.
	bool mbSwapchainRecreateDeferred = false;

	std::unique_ptr<InstanceManager> mpInstanceManager;
	std::unique_ptr<DeviceManager> mpDeviceManager;
	std::unique_ptr<SwapchainManager> mpSwapchainManager;
	std::unique_ptr<CommandBufferManager> mpCommandBufferManager;
	std::unique_ptr<BufferManager> mpBufferManager;
	std::unique_ptr<TextureManager> mpTextureManager;
	std::unique_ptr<Islands> mpIslands;
	std::unique_ptr<PipelineManager> mpPipelineManager;
	std::unique_ptr<ParticleManager> mpParticleManager;

	std::unique_ptr<ImGuiManager> mpImGuiManager;

	common::Timer mRenderFrameTimer;
	int64_t miRenderFrameDeltaNs = 0;

	common::InTheLastSecond mRendersInTheLastSecond;

	// One-shot capture mailboxes filled off the render path (agent Drain / F9 toggle), consumed once at the
	// RenderMainPresentAcquire capture site (between the UI submit and Present — see the deadlock note there).
	std::optional<ScreenshotRequest> mScreenshotRequest;
	std::optional<DumpRenderTargetRequest> mDumpRenderTargetRequest;

	std::unordered_set<std::string> mDebugNames;

private:

	// Queries surface caps into rVkSurfaceCapabilitiesKHR and returns true when the swapchain recreate must defer: a
	// zero-area defined currentExtent (window off-screen/minimized) with the tier still below kSurface. Not const —
	// the CHECK_VK inside can escalate meDestroyType to kSurface (surface-lost), which the < kSurface term then excludes.
	bool SurfaceExtentZeroArea(VkSurfaceCapabilitiesKHR& rVkSurfaceCapabilitiesKHR);

	template <typename T>
	void PollSetting(Wrapper& rWrapper, const char* pcLabel, DestroyType eTier, std::optional<DestroyFlags> oeFlag = std::nullopt, bool bGate = true);
};

inline Graphics* gpGraphics = nullptr;

} // namespace engine

#endif // defined(BT_CLIENT)
