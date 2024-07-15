#pragma once

#include "Islands.h"
#include "Debug/EnumToString.h"
#include "Managers/BufferManager.h"
#include "Managers/CommandBufferManager.h"
#include "Managers/DeviceManager.h"
#include "Managers/InstanceManager.h"
#include "Managers/ParticleManager.h"
#include "Managers/PipelineManager.h"
#include "Managers/ShaderManager.h"
#include "Managers/SwapchainManager.h"
#include "Managers/TextManager.h"
#include "Managers/TextureManager.h"
#include "Profile/ProfileManager.h"
#include "Ui/UiManager.h"
#include "Ui/Wrapper.h"

namespace game
{

struct Frame;

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

inline VkExtent2D gWantedFramebufferExtent2D {};

std::tuple<int64_t, int64_t> FullDetail();
float SmokeSimulationPixels();

template <class T, class... Ts>
void RenderGlobalList(int64_t iCommandBuffer, const game::Frame& __restrict rFrame, [[maybe_unused]] T* pCurrentT, const Ts&... nextTs)
{
	T::RenderGlobal(iCommandBuffer, rFrame);

	if constexpr (sizeof...(nextTs) > 0)
	{
		RenderGlobalList(iCommandBuffer, rFrame, nextTs...);
	}
}

template <class T, class... Ts>
void RenderMainList(int64_t iCommandBuffer, const game::Frame& __restrict rFrame, [[maybe_unused]] T* pCurrentT, const Ts&... nextTs)
{
	T::RenderMain(iCommandBuffer, rFrame);

	if constexpr (sizeof...(nextTs) > 0)
	{
		RenderMainList(iCommandBuffer, rFrame, nextTs...);
	}
}

class Graphics
{
public:

	Graphics(HINSTANCE hinstance, HWND hwnd);
	~Graphics();

	Graphics() = delete;

	void RenderGlobal(const game::Frame& __restrict rFrame);
	void RenderMainImagePresentAcquire(const game::Frame& __restrict rFrame);

	void RenderPresentAcquire(const game::Frame& __restrict rFrame)
	{
		RenderGlobal(rFrame);
		RenderMainImagePresentAcquire(rFrame);
	}

	void Create();
	void Refresh();
	bool Destroy();

	HINSTANCE mHinstance = nullptr;
	HWND mHwnd = nullptr;
	int64_t miMonitorRefreshRate = 60;
	VkExtent2D mFramebufferExtent2D = gWantedFramebufferExtent2D;

	DestroyType meDestroyType = DestroyType::kNone;

	std::unique_ptr<InstanceManager> mpInstanceManager;
	std::unique_ptr<DeviceManager> mpDeviceManager;
	std::unique_ptr<ShaderManager> mpShaderManager;
	std::unique_ptr<SwapchainManager> mpSwapchainManager;
	std::unique_ptr<CommandBufferManager> mpCommandBufferManager;
	std::unique_ptr<BufferManager> mpBufferManager;
	std::unique_ptr<TextureManager> mpTextureManager;
	std::unique_ptr<TextManager> mpTextManager;
	std::unique_ptr<Islands> mpIslands;
	std::unique_ptr<UiManager> mpUiManager;
	std::unique_ptr<PipelineManager> mpPipelineManager;
	std::unique_ptr<ParticleManager> mpParticleManager;

	common::InTheLastSecond mRendersInTheLastSecond;
};

inline Graphics* gpGraphics = nullptr;

} // namespace engine
