#pragma once

#include "Islands.h"
#include "OneShotCommandBuffer.h"
#include "Debug/EnumToString.h"
#include "Graphics/Camera.h"
#include "Managers/BufferManager.h"
#include "Managers/CommandBufferManager.h"
#include "Managers/DeviceManager.h"
#include "Managers/ImGuiManager.h"
#include "Managers/InstanceManager.h"
#include "Managers/ParticleManager.h"
#include "Managers/PipelineManager.h"
#include "Managers/ShaderManager.h"
#include "Managers/SwapchainManager.h"
#include "Managers/TextManager.h"
#include "Managers/TextureManager.h"
#include "Profile/ProfileManager.h"

namespace game
{

struct FrameInterpolate;

}

namespace engine
{

struct RenderFrame;

enum DestroyType
{
	kNone,
	kCommandBuffers,
	kPipelines,
	kSwapchain,
	kSurface,
};

enum class DestroyFlags : uint16_t
{
	kSamplers           = 0x0001,
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

	void RenderGlobal(const game::Frame& __restrict rFrame);
	void RenderMainPresentAcquire(int64_t iCommandBuffer);
	void WaitForRender();

	void RenderPresentAcquire(const game::Frame& __restrict rFrame)
	{
		RenderGlobal(rFrame);
		RenderMainPresentAcquire(gpSwapchainManager->miFramebufferIndex);
	}

	void Create();
	void Refresh();
	bool Destroy();
	void RecreateResources();

	HINSTANCE mHinstance = nullptr;
	HWND mHwnd = nullptr;
	int64_t miMonitorRefreshRate = 60;
	uint64_t miFrameCounter = 0;
	VkExtent2D mFramebufferExtent2D = gWantedFramebufferExtent2D;

	DestroyType meDestroyType = DestroyType::kNone;
	DestroyFlags_t mDestroyFlags;
	VkSwapchainKHR mOldVkSwapchainKHR = VK_NULL_HANDLE;

	std::unique_ptr<InstanceManager> mpInstanceManager;
	std::unique_ptr<DeviceManager> mpDeviceManager;
	std::unique_ptr<ShaderManager> mpShaderManager;
	std::unique_ptr<SwapchainManager> mpSwapchainManager;
	std::unique_ptr<CommandBufferManager> mpCommandBufferManager;
	std::unique_ptr<BufferManager> mpBufferManager;
	std::unique_ptr<TextureManager> mpTextureManager;
	std::unique_ptr<TextManager> mpTextManager;
	std::unique_ptr<Islands> mpIslands;
	std::unique_ptr<PipelineManager> mpPipelineManager;
	std::unique_ptr<ParticleManager> mpParticleManager;

	std::unique_ptr<ImGuiManager> mpImGuiManager;

	common::InTheLastSecond mRendersInTheLastSecond;

	std::unique_ptr<game::FrameInterpolate> mpFrameInterpolate;
	std::future<void> mRenderFuture;

#if defined(ENABLE_VULKAN_DEBUG_LAYERS)
	std::unordered_set<std::string> mDebugNames;
#endif
};

inline Graphics* gpGraphics = nullptr;

} // namespace engine
