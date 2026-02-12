#pragma once

namespace game
{

class HudScreen
{
public:

	void Initialize();
	void Shutdown();
	void Render();

private:

	void RenderShieldBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize);
	void RenderArmorBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize);

	VkDescriptorSet mShieldIconVkDescriptorSet = VK_NULL_HANDLE;
	VkDescriptorSet mArmorIconVkDescriptorSet = VK_NULL_HANDLE;
	bool mbTexturesRequested = false;
};

} // namespace game
