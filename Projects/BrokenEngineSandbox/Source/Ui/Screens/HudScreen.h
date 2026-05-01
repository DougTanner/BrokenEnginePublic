#pragma once

namespace game
{

class HudScreen
{
public:

	void Render();

private:

	void RenderFleetPanel();
	void RenderFocusedPlayerPanel();

#if defined(BT_CLIENT)
	engine::NetworkUiControl<int64_t> mCreateFleetToggle {};
	engine::NetworkUiControl<int64_t> mSpawnIntoFleetToggle {};
	engine::NetworkUiControl<int64_t> mDeleteFleetToggle {};

	struct SlidePanelState
	{
		float fOpenness = 0.0f;
		ImVec2 vLastSize {};
	};
	static float UpdateSlideAndGetOffsetX(SlidePanelState& rState, ImVec2 vAnchor, float fPivotX, float fSidePivotSign);
	SlidePanelState mFleetSlide {};
	SlidePanelState mFocusedPlayerSlide {};
#endif
};

} // namespace game
