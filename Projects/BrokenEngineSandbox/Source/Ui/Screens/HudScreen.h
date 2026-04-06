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

	void RenderBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize, float fValue, float fHalfWidthPerPoint, float fBarYSign, ImU32 uiBarColor, VkDescriptorSet vkIconDescriptorSet);
	void RenderPlayerPanel();
	void RenderFocusedPlayerPanel();

	VkDescriptorSet mShieldIconVkDescriptorSet = VK_NULL_HANDLE;
	VkDescriptorSet mArmorIconVkDescriptorSet = VK_NULL_HANDLE;
	bool mbTexturesRequested = false;
};

} // namespace game
