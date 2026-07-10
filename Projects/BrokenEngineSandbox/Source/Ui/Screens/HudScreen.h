#pragma once

namespace game
{

class HudScreen
{
public:

	void Render();

private:

	void RenderFleetPanel(float fTarget);
	void RenderFocusedPlayerPanel(float fTarget);

#if defined(BT_CLIENT)
	engine::NetworkUiControl<int64_t> mCreateFleetToggle {};
	engine::NetworkUiControl<int64_t> mSpawnIntoFleetToggle {};
	engine::NetworkUiControl<int64_t> mDeleteFleetToggle {};

	struct SlidePanelState
	{
		float fOpenness = 0.0f;
		ImVec2 vLastSize {};
	};
	static float ComputeMouseOpennessTarget(ImVec2 vFixedExtent, ImVec2 vAnchor, float fPivotX);
	static float UpdateSlideAndGetEdgeX(SlidePanelState& rState, ImVec2 vAnchor, float fSidePivotSign, float fTarget);
	static float PanelWidth();
	SlidePanelState mFleetSlide {};
	SlidePanelState mFocusedPlayerSlide {};
	float mfTimeWantingForceOpen = 0.0f;
	bool mbPreviousForceOpen = false;
#endif
};

} // namespace game
