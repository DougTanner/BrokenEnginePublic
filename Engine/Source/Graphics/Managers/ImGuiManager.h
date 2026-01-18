#pragma once

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
};

inline ImGuiManager* gpImGuiManager = nullptr;

} // namespace engine
