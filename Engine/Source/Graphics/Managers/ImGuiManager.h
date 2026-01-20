#pragma once

#include "Ui/Screens/TweaksScreen.h"

namespace engine
{

class ImGuiManager
{
public:

	ImGuiManager(HWND hwnd);
	~ImGuiManager();

	void Submit(int64_t iFramebuffer);

private:

	void CreateRenderPass();
	void CreateFramebuffers();

	VkRenderPass mImGuiRenderPass = VK_NULL_HANDLE;
	std::vector<VkFramebuffer> mImGuiFramebuffers;
	ImDrawData* mpDrawData = nullptr;
	TweaksScreen mTweaksScreen;
};

inline ImGuiManager* gpImGuiManager = nullptr;

} // namespace engine
