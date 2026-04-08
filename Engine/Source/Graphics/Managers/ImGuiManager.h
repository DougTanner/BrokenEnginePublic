#pragma once

namespace game
{
class DeathMenuScreen;
class GraphicsMenuScreen;
class HudScreen;
class MainMenuScreen;
class ModalScreen;
class PauseMenuScreen;
class SoundMenuScreen;
class TweaksScreen;
}

namespace engine
{

class ImGuiManager
{
public:

	ImGuiManager(HWND hwnd);
	~ImGuiManager();

	void Prepare(int64_t iFramebuffer);
	void Submit(int64_t iFramebuffer);
	void RecreateSamplerDependencies();
	void RegisterOpaqueRect(const ImVec2& pos, const ImVec2& size);

	static constexpr int64_t kiMaxUiRects = 32;

	ImFont* mpChineseFont = nullptr;
	std::unique_ptr<game::TweaksScreen> mpTweaksScreen;

	VkBuffer mUiPrepassIndirectVkBuffer = VK_NULL_HANDLE;

private:

	void CreateRenderPass();
	void CreateFramebuffers();
	void UpdateUiRectBuffers(int64_t iFramebuffer);

	VkRenderPass mImGuiRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> mImGuiFramebuffers;
	ImDrawData* mpDrawData = nullptr;
	std::unique_ptr<game::MainMenuScreen> mpMainMenuScreen;
	std::unique_ptr<game::ModalScreen> mpModalScreen;
	std::unique_ptr<game::PauseMenuScreen> mpPauseMenuScreen;
	std::unique_ptr<game::GraphicsMenuScreen> mpGraphicsMenuScreen;
	std::unique_ptr<game::SoundMenuScreen> mpSoundMenuScreen;
	std::unique_ptr<game::DeathMenuScreen> mpDeathMenuScreen;
	std::unique_ptr<game::HudScreen> mpHudScreen;

	VmaAllocation mUiPrepassIndirectVmaAllocation = VK_NULL_HANDLE;
	VkDrawIndirectCommand* mpUiPrepassIndirectMapped = nullptr;

	int64_t miOpaqueRectCount = 0;
	XMFLOAT4 mOpaqueRects[kiMaxUiRects] {};
};

inline ImGuiManager* gpImGuiManager = nullptr;

} // namespace engine
