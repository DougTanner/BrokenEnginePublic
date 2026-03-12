#pragma once

namespace game
{

struct Frame;
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
class TextManager;
class TextureManager;

struct RenderFrame;

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
	kTerrainMesh        = 0x0002,
	kShadowTextures     = 0x0004,
	kObjectShadows      = 0x0008,
	kLightingTextures   = 0x0010,
	kWaterMesh          = 0x0020,
	kTerrainElevation   = 0x0040,
	kTerrainColor       = 0x0080,
	kTerrainNormal      = 0x0100,
	kTerrainAO          = 0x0200,
	kSmokeTextures      = 0x0400,
};
using DestroyFlags_t = common::Flags<DestroyFlags>;

inline VkExtent2D gWantedFramebufferExtent2D {};

std::tuple<int64_t, int64_t> FullDetail();
float SmokeSimulationPixels();

class Graphics
{
public:

	Graphics(HINSTANCE hinstance, HWND hwnd);
	~Graphics();

	Graphics() = delete;

	void RenderGlobal(const game::Frame& __restrict rFrame, float fCurrentTime);
	void RenderMainPresentAcquire(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord);
	void Create();
	void Refresh();
	bool Destroy();
	void RecreateResources();

	HINSTANCE mHinstance = nullptr;
	HWND mHwnd = nullptr;
	int64_t miMonitorRefreshRate = 60;
	uint64_t muiFrameCounter = 0;
	VkExtent2D mFramebufferExtent2D = gWantedFramebufferExtent2D;

	DestroyType meDestroyType = DestroyType::kNone;
	DestroyFlags_t mDestroyFlags;
	VkSwapchainKHR mOldVkSwapchainKHR = VK_NULL_HANDLE;

	std::unique_ptr<InstanceManager> mpInstanceManager;
	std::unique_ptr<DeviceManager> mpDeviceManager;
	std::unique_ptr<SwapchainManager> mpSwapchainManager;
	std::unique_ptr<CommandBufferManager> mpCommandBufferManager;
	std::unique_ptr<BufferManager> mpBufferManager;
	std::unique_ptr<TextureManager> mpTextureManager;
	std::unique_ptr<TextManager> mpTextManager;
	std::unique_ptr<Islands> mpIslands;
	std::unique_ptr<PipelineManager> mpPipelineManager;
	std::unique_ptr<ParticleManager> mpParticleManager;

	std::unique_ptr<ImGuiManager> mpImGuiManager;

	common::Timer mRenderFrameTimer;
	int64_t miRenderFrameDeltaNs = 0;

	common::InTheLastSecond mRendersInTheLastSecond;

	std::unordered_map<GridCoord, game::FrameInterpolate> mRenderInterpolates;

	std::unordered_set<std::string> mDebugNames;
};

inline Graphics* gpGraphics = nullptr;

} // namespace engine
