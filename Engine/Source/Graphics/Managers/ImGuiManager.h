#pragma once

#include "Ui/Screens/TweaksScreen.h"

#include "Ui/Screens/DeathMenuScreen.h"
#include "Ui/Screens/GraphicsMenuScreen.h"
#include "Ui/Screens/HudScreen.h"
#include "Ui/Screens/MainMenuScreen.h"
#include "Ui/Screens/PauseMenuScreen.h"
#include "Ui/Screens/SoundMenuScreen.h"

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
	TweaksScreen mTweaksScreen;
	game::MainMenuScreen mMainMenuScreen;
	game::PauseMenuScreen mPauseMenuScreen;
	game::GraphicsMenuScreen mGraphicsMenuScreen;
	game::SoundMenuScreen mSoundMenuScreen;
	game::DeathMenuScreen mDeathMenuScreen;
	game::HudScreen mHudScreen;
};

inline ImGuiManager* gpImGuiManager = nullptr;

} // namespace engine
