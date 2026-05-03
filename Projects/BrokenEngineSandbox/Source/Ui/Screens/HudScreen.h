#pragma once

namespace game
{

class HudScreen
{
public:

	void Render();
	void RenderSunMoonDebugOverlay(); // DT: TEMP

private:

	void RenderFleetPanel(bool bForceOpen, float fMouseTarget);
	void RenderFocusedPlayerPanel(float fLeftMouseTarget);

#if defined(BT_CLIENT)
	engine::NetworkUiControl<int64_t> mCreateFleetToggle {};
	engine::NetworkUiControl<int64_t> mSpawnIntoFleetToggle {};
	engine::NetworkUiControl<int64_t> mDeleteFleetToggle {};

	struct SlidePanelState
	{
		float fOpenness = 0.0f;
		ImVec2 vLastSize {};
	};
	static float ComputeMouseOpennessTarget(ImVec2 vLastSize, ImVec2 vAnchor, float fPivotX);
	static float UpdateSlideAndGetOffsetX(SlidePanelState& rState, ImVec2 vAnchor, float fSidePivotSign, float fTarget);
	SlidePanelState mFleetSlide {};
	SlidePanelState mFocusedPlayerSlide {};
	float mfTimeWantingForceOpen = 0.0f;
	bool mbPreviousWantsForceOpen = false;
	bool mbPreviousForceLeftOpen = false;
#endif
};

} // namespace game
