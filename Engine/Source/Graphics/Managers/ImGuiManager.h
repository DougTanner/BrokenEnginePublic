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

	void Submit(int64_t iFramebuffer);
	void RecreateSamplerDependencies();

	ImFont* mpChineseFont = nullptr;

private:

	void CreateRenderPass();
	void CreateFramebuffers();

	VkRenderPass mImGuiRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> mImGuiFramebuffers;
	ImDrawData* mpDrawData = nullptr;
	std::unique_ptr<game::TweaksScreen> mpTweaksScreen;
	std::unique_ptr<game::MainMenuScreen> mpMainMenuScreen;
	std::unique_ptr<game::ModalScreen> mpModalScreen;
	std::unique_ptr<game::PauseMenuScreen> mpPauseMenuScreen;
	std::unique_ptr<game::GraphicsMenuScreen> mpGraphicsMenuScreen;
	std::unique_ptr<game::SoundMenuScreen> mpSoundMenuScreen;
	std::unique_ptr<game::DeathMenuScreen> mpDeathMenuScreen;
	std::unique_ptr<game::HudScreen> mpHudScreen;
};

inline ImGuiManager* gpImGuiManager = nullptr;

} // namespace engine
