#include "HudScreen.h"

#if defined(BT_CLIENT)

#include "Fleet.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Game.h"
#include "MenuUtils.h"

namespace
{

constexpr float kfActivationDistancePixels = 350.0f;
constexpr float kfSlideRate = 8.0f;
constexpr float kfForceOpenGracePeriodSeconds = 2.0f;

} // namespace

namespace game
{

float HudScreen::ComputeMouseOpennessTarget(ImVec2 vFixedExtent, ImVec2 vAnchor, float fPivotX)
{
	const float fRectMinX = vAnchor.x - fPivotX * vFixedExtent.x;
	const float fRectMaxX = fRectMinX + vFixedExtent.x;
	const float fRectMinY = vAnchor.y;
	const float fRectMaxY = vAnchor.y + vFixedExtent.y;

	const ImVec2 vMouse = ImGui::GetIO().MousePos;
	const float fDx = std::max({fRectMinX - vMouse.x, 0.0f, vMouse.x - fRectMaxX});
	const float fDy = std::max({fRectMinY - vMouse.y, 0.0f, vMouse.y - fRectMaxY});
	const float fDistance = std::sqrt(fDx * fDx + fDy * fDy);

	return 1.0f - std::clamp(fDistance / (kfActivationDistancePixels * engine::UiScale()), 0.0f, 1.0f);
}

float HudScreen::UpdateSlideAndGetEdgeX(SlidePanelState& rState, ImVec2 vAnchor, float fSidePivotSign, float fTarget)
{
	ImGuiIO& rIo = ImGui::GetIO();

	// Caller must set SetNextWindowPos pivot so the returned x IS the panel's off-screen edge:
	//   right panel (fSidePivotSign > 0): pivot (0.0, 0) — returned x is the left edge
	//   left  panel (fSidePivotSign < 0): pivot (1.0, 0) — returned x is the right edge
	// At openness=0 the off-screen edge is fixed at exactly 1 pixel beyond the screen boundary,
	// width-independent — keeps the panel fully offscreen regardless of its measured width.
	const float fOffscreenX = (fSidePivotSign > 0.0f) ? rIo.DisplaySize.x + 1.0f : -1.0f;

	if (rState.vLastSize.x <= 0.0f)
	{
		return fOffscreenX;
	}

	// rIo.DeltaTime is uncapped wall-clock time; a long UI hitch pushes the interpolant past 1 and overshoots.
	rState.fOpenness += (fTarget - rState.fOpenness) * std::min(common::ExponentialInterpolant(kfSlideRate, rIo.DeltaTime), 1.0f);

	// At openness=1 the on-screen anchor edge sits at vAnchor.x (offset by size to flip pivot side).
	const float fOnscreenX = (fSidePivotSign > 0.0f) ? (vAnchor.x - rState.vLastSize.x) : (vAnchor.x + rState.vLastSize.x);
	return std::lerp(fOffscreenX, fOnscreenX, rState.fOpenness);
}

void HudScreen::Render()
{
	// Ensure tick prefix for the auto-unhide log — Render() runs outside ClientUpdate's LogTickScope.
	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	// Force-open the fleet panel when the focused fleet has no presence in any subscribed frame.
	// Iterating all subscribed frames (not just mClientGridCoord) tolerates cell-boundary crossings,
	// where the player's snapshot has migrated to a neighbor before mClientGridCoord catches up.
	bool bWantsForceOpen = false;
	const char* pcWantReason = "fleet member present";
	int64_t iSubscribedFrameCount = 0;

	if (!gpGame->ClientPlayerId().IsValid())
	{
		bWantsForceOpen = true;
		pcWantReason = "ClientPlayerId invalid";
	}
	else if (const Fleet* pFleet = gpGame->FocusedFleet(); pFleet == nullptr)
	{
		bWantsForceOpen = true;
		pcWantReason = "no focused fleet";
	}
	else
	{
		bool bFoundAny = false;
		for (const auto& [coord, frames] : gpGame->mCoordFrames)
		{
			if (frames.iSnapshotCount == 0)
			{
				continue;
			}
			++iSubscribedFrameCount;
			const PlayersPostRender& rPlayers = *gpGame->RenderFrame(coord).postRender.pPlayers;
			for (int64_t i = 0; i < rPlayers.iCount && !bFoundAny; ++i)
			{
				const engine::global_id_t globalPlayerId = rPlayers.pGlobalPlayerIds[i];
				for (const FleetMember& rMember : pFleet->members)
				{
					if (rMember.globalPlayerId == globalPlayerId)
					{
						bFoundAny = true;
						break;
					}
				}
			}
			if (bFoundAny)
			{
				break;
			}
		}
		if (!bFoundAny)
		{
			bWantsForceOpen = true;
			pcWantReason = (iSubscribedFrameCount == 0)
				? "no subscribed snapshots"
				: "no fleet members in any subscribed frame";
		}
	}

	// Grace period: only force-open once the want-state has been sustained. Absorbs the brief gap during cell-boundary
	// hand-offs when the player snapshot is momentarily absent from every subscribed frame, plus ClientPlayerId blips.
	ImGuiIO& rIo = ImGui::GetIO();
	if (bWantsForceOpen)
	{
		mfTimeWantingForceOpen += rIo.DeltaTime;
	}
	else
	{
		mfTimeWantingForceOpen = 0.0f;
	}
	const bool bForceOpen = (mfTimeWantingForceOpen >= kfForceOpenGracePeriodSeconds);

	// Durable log on rising edge of the genuine auto-un-hide trigger — fires once per recovery event.
	// kWarning clears both the compile floor (keLogLevelDefault, kDebug) and the runtime default threshold (kInfo).
	if (bForceOpen && !mbPreviousForceOpen)
	{
		LOG(kDefault, kWarning, "HUD auto-unhide reason: {} coord: ({},{}) frames: {}",
			pcWantReason, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y, iSubscribedFrameCount);
	}
	mbPreviousForceOpen = bForceOpen;

	// Right panel content gate: it has nothing useful to show without a focused player in current snapshot.
	std::optional<int64_t> oPlayerIndex;
	{
		auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		if (it != gpGame->mCoordFrames.end() && it->second.iSnapshotCount > 0)
		{
			oPlayerIndex = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
		}
	}
	const bool bRightHasContent = oPlayerIndex.has_value();

	// Mouse proximity to either anchor opens both panels (strict sync for the mouse path).
	const ImVec2 vLeftAnchor(rIo.DisplaySize.x * kfHudEdgeMarginFraction, rIo.DisplaySize.y * kfHudPanelTopFraction);
	const ImVec2 vRightAnchor(rIo.DisplaySize.x * (1.0f - kfHudEdgeMarginFraction), rIo.DisplaySize.y * kfHudPanelTopFraction);

	// Hover activation zone is a fixed-extent strip (PanelWidth x max-height fraction at the anchor), decoupled from the
	// panels' content-driven live size so hover behavior is unchanged even as the panels visually shrink. Measure
	// PanelWidth() under the same scale+font scopes the panels render with so the strip width matches them exactly.
	ImVec2 vHoverExtent {};
	{
		ScopedMenuScale menuScale;
		ScopedMenuFont menuFont;
		vHoverExtent = ImVec2(PanelWidth(), rIo.DisplaySize.y * kfHudPanelMaxHeightFraction);
	}
	const float fMouseLeft = ComputeMouseOpennessTarget(vHoverExtent, vLeftAnchor, 0.0f);
	const float fMouseRight = ComputeMouseOpennessTarget(vHoverExtent, vRightAnchor, 1.0f);
	const float fMouseTarget = std::max(fMouseLeft, fMouseRight);

	// Final shared targets. When right has content: both panels see max(force, mouse) — strict sync.
	// When right has no content: left can still auto-un-hide (force only), right stays hidden.
	const float fForceTarget = bForceOpen ? 1.0f : 0.0f;
	const float fLeftTarget = bRightHasContent ? std::max(fForceTarget, fMouseTarget) : fForceTarget;
	const float fRightTarget = bRightHasContent ? std::max(fForceTarget, fMouseTarget) : 0.0f;

	RenderFleetPanel(fLeftTarget);
	RenderFocusedPlayerPanel(fRightTarget);
}

float HudScreen::PanelWidth()
{
	// Fixed worst-case templates measured under the pushed menu font (caller pushes ScopedMenuFont first). Fixed
	// templates — not live content — keep the width stable as fleet members churn; measuring under the font auto-tracks
	// gUiFontScale/UiScale() with no magnifying multiplier.
	const ImGuiStyle& rStyle = ImGui::GetStyle();

	// Member-row template: content-spanning Selectable rows.
	const float fMemberRowWidth = ImGui::CalcTextSize("Ship 88 (-888,-888) #8888888888").x;

	// Nav-row template: [<] 88/88 [>] [+] [-]. Padding-dominated — at low gUiFontScale text shrinks but the per-button
	// FramePadding and per-joint ItemSpacing don't, so measure them explicitly: 4 buttons × 2 edges (8× FramePadding.x),
	// 4 SameLine joints (4× ItemSpacing.x). Otherwise the row can exceed the text-only width and clip trailing buttons.
	const float fNavTextWidth = ImGui::CalcTextSize("[<]88/88[>][+][-]").x;
	const float fNavRowWidth = fNavTextWidth + 8.0f * rStyle.FramePadding.x + 4.0f * rStyle.ItemSpacing.x;

	// ScrollbarSize added unconditionally (not gated on list length) so the width stays frame-to-frame stable when a
	// vertical scrollbar appears on long fleet lists — otherwise it would clip the exact-fit member rows.
	return std::max(fMemberRowWidth, fNavRowWidth) + rStyle.WindowPadding.x * 2.0f + rStyle.ScrollbarSize;
}

void HudScreen::RenderFleetPanel(float fTarget)
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	const ImVec2 vAnchor(rIo.DisplaySize.x * kfHudEdgeMarginFraction, rIo.DisplaySize.y * kfHudPanelTopFraction);
	const float fEdgeX = UpdateSlideAndGetEdgeX(mFleetSlide, vAnchor, -1.0f, fTarget);
	ImGuiWindowFlags eFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
	ScopedMenuFont menuFont;
	// Content-driven height: auto-resize to the fleet list, capped at kfHudPanelMaxHeightFraction (long lists scroll). Width
	// pinned to PanelWidth() via the matching min/max constraint x.
	const float fPanelWidth = PanelWidth();
	const float fMaxHeight = rIo.DisplaySize.y * kfHudPanelMaxHeightFraction;
	ImGui::SetNextWindowSize(ImVec2(fPanelWidth, 0.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(fPanelWidth, 0.0f), ImVec2(fPanelWidth, fMaxHeight));
	ImGui::SetNextWindowPos(ImVec2(fEdgeX, vAnchor.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::Begin("FleetPanel", nullptr, eFlags);
	mFleetSlide.vLastSize = ImGui::GetWindowSize();
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// Border + accent strip only — the opaque themed WindowBg must stay intact for RegisterOpaqueRect occlusion
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	int64_t iFleetCount = gpGame->FleetCount();

	// Update fleet toggle: clears pending when fleet count changes
	mCreateFleetToggle.Update(iFleetCount);

	// Fleet navigation row: [<] fleet_index/fleet_count [>] [+]
	ImGui::BeginDisabled(!gpGame->CanFocusPrevFleet());
	if (ImGui::Button("[<]"))
	{
		gpGame->FocusPrevFleet();
		gpClientSession->UpdateDesiredCoords(SubscriptionChangeReason::kFocusPrevFleet);
		LOG(kDefault, kVerbose, "HUD FocusPrevFleet NewIndex: {} FleetCount: {}", gpGame->FocusedFleetIndex(), iFleetCount);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (iFleetCount > 0)
	{
		ImGui::Text("%lld/%lld", gpGame->FocusedFleetIndex() + 1, iFleetCount);
	}
	else
	{
		ImGui::Text("0/0");
	}
	ImGui::SameLine();

	ImGui::BeginDisabled(!gpGame->CanFocusNextFleet());
	if (ImGui::Button("[>]"))
	{
		gpGame->FocusNextFleet();
		gpClientSession->UpdateDesiredCoords(SubscriptionChangeReason::kFocusNextFleet);
		LOG(kDefault, kVerbose, "HUD FocusNextFleet NewIndex: {} FleetCount: {}", gpGame->FocusedFleetIndex(), iFleetCount);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(mCreateFleetToggle.IsPending());
	if (ImGui::Button("[+]##Fleet"))
	{
		if (gpClientSession != nullptr)
		{
			mCreateFleetToggle.SetPending();
			gpClientSession->SendCreateFleetRequest();
			LOG(kDefault, kVerbose, "HUD CreateFleetRequest FleetCount: {}", iFleetCount);
		}
	}
	ImGui::EndDisabled();

	const Fleet* pFleet = gpGame->FocusedFleet();

	// Delete empty fleet button
	mDeleteFleetToggle.Update(iFleetCount);
	bool bCanDelete = pFleet != nullptr && pFleet->members.empty();
	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanDelete || mDeleteFleetToggle.IsPending());
	if (ImGui::Button("[-]##Fleet"))
	{
		if (pFleet != nullptr && gpClientSession != nullptr)
		{
			mDeleteFleetToggle.SetPending();
			gpClientSession->SendDeleteFleetRequest(pFleet->guid);
			LOG(kDefault, kVerbose, "HUD DeleteFleetRequest Fleet: ({},{}) FleetCount: {}", pFleet->guid.uiHigh, pFleet->guid.uiLow, iFleetCount);
		}
	}
	ImGui::EndDisabled();

	// Fleet member list
	if (pFleet != nullptr)
	{
		ImGui::Separator();

		// Update spawn into fleet toggle based on member count
		mSpawnIntoFleetToggle.Update(std::ssize(pFleet->members));

		for (int64_t i = 0; i < std::ssize(pFleet->members); ++i)
		{
			const FleetMember& rMember = pFleet->members.at(static_cast<size_t>(i));
			bool bSelected = (i == gpGame->FocusedPlayerInFleetIndex());

			// Find coord for display
			engine::GridCoord memberCoord {};
			for (int64_t j = 0; j < std::ssize(gpGame->mClientPlayerIds); ++j)
			{
				if (gpGame->mClientPlayerIds.at(j) == rMember.globalPlayerId)
				{
					memberCoord = gpGame->mClientPlayerCoords.at(j);
					break;
				}
			}

			ImGui::PushID(static_cast<int>(i));
			if (rMember.bAlive)
			{
				char pcLabel[64];
				std::snprintf(pcLabel, sizeof(pcLabel), "Ship %lld (%d,%d) #%lld", i + 1, memberCoord.x, memberCoord.y, rMember.globalPlayerId.iValue);
				if (ImGui::Selectable(pcLabel, bSelected))
				{
					gpGame->SelectPlayerInFleet(i);
					gpClientSession->UpdateDesiredCoords(SubscriptionChangeReason::kSelectPlayer);
				}
			}
			else
			{
				char pcLabel[64];
				std::snprintf(pcLabel, sizeof(pcLabel), "Ship %lld [DEAD] #%lld", i + 1, rMember.globalPlayerId.iValue);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
				if (ImGui::Selectable(pcLabel, false))
				{
					if (gpClientSession != nullptr)
					{
						gpClientSession->SendRespawnInFleetRequest(pFleet->guid, i);
						LOG(kDefault, kVerbose, "HUD RespawnInFleet Fleet: ({},{}) Member: {}", pFleet->guid.uiHigh, pFleet->guid.uiLow, i);
					}
				}
				ImGui::PopStyleColor();
			}
			ImGui::PopID();
		}

		// Add player button at bottom of list
		ImGui::BeginDisabled(mSpawnIntoFleetToggle.IsPending());
		if (ImGui::Button("[+]##Player"))
		{
			if (gpClientSession != nullptr)
			{
				mSpawnIntoFleetToggle.SetPending();
				gpClientSession->SendSpawnIntoFleetRequest(pFleet->guid);
				LOG(kDefault, kVerbose, "HUD SpawnIntoFleet Fleet: ({},{})", pFleet->guid.uiHigh, pFleet->guid.uiLow);
			}
		}
		ImGui::EndDisabled();

		// Fleet navigation delay slider
		ImGui::Separator();
		gpGame->mNavigationDelayControl.Update(pFleet->fNavigationDelay);
		ImGui::BeginDisabled(gpGame->mNavigationDelayControl.IsPending());
		static float sfNavigationDelayEditValue = 0.0f;
		float fSliderValue = pFleet->fNavigationDelay;
		// Reserve the trailing label's width — AlwaysAutoResize windows default the item width to the full content
		// width, which would push the label past the clip edge
		ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Nav Delay").x + ImGui::GetStyle().ItemInnerSpacing.x));
		if (ImGui::SliderFloat("Nav Delay", &fSliderValue, 0.0f, 60.0f))
		{
			sfNavigationDelayEditValue = fSliderValue;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (gpClientSession != nullptr)
			{
				gpGame->mNavigationDelayControl.SetPending();
				gpClientSession->SendFleetNavigationDelayRequest(pFleet->guid, sfNavigationDelayEditValue);
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::End();
}

void HudScreen::RenderFocusedPlayerPanel(float fTarget)
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	std::optional<int64_t> oPlayerIndex = std::nullopt;
	{
		auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		if (it != gpGame->mCoordFrames.end() && it->second.iSnapshotCount > 0)
		{
			oPlayerIndex = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
		}
	}

	const ImVec2 vAnchor(rIo.DisplaySize.x * (1.0f - kfHudEdgeMarginFraction), rIo.DisplaySize.y * kfHudPanelTopFraction);
	const float fEdgeX = UpdateSlideAndGetEdgeX(mFocusedPlayerSlide, vAnchor, 1.0f, fTarget);
	ImGuiWindowFlags eFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	ScopedMenuFont menuFont;
	// Match the left FleetPanel's size exactly (symmetry): same PanelWidth(), height forced to the left panel's live height
	// captured earlier this frame (RenderFleetPanel runs first). First frame (vLastSize.y still zero): fall back to the cap.
	const float fLeftHeight = (mFleetSlide.vLastSize.y > 0.0f) ? mFleetSlide.vLastSize.y : (rIo.DisplaySize.y * kfHudPanelMaxHeightFraction);
	ImGui::SetNextWindowSize(ImVec2(PanelWidth(), fLeftHeight), ImGuiCond_Always);
	ImGui::SetNextWindowPos(ImVec2(fEdgeX, vAnchor.y), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
	ImGui::Begin("FocusedPlayerPanel", nullptr, eFlags);
	mFocusedPlayerSlide.vLastSize = ImGui::GetWindowSize();
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// Border + accent strip only — the opaque themed WindowBg must stay intact for RegisterOpaqueRect occlusion
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	if (oPlayerIndex.has_value())
	{
		PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
		bool bUseMissiles = static_cast<bool>(rPlayers.pFlags[*oPlayerIndex] & PlayerFlags::kUseMissiles);
		gpGame->mWeaponModeToggle.Update(bUseMissiles);

		const char* pLabel = bUseMissiles ? "Missiles" : "Blasters";
		const ImGuiStyle& rStyle = ImGui::GetStyle();
		const ImVec2 vLabelSize = ImGui::CalcTextSize(pLabel);
		const ImVec2 vButtonSize(vLabelSize.x + 2.0f * rStyle.FramePadding.x, vLabelSize.y + 2.0f * rStyle.FramePadding.y);
		const ImVec2 vAvailable = ImGui::GetContentRegionAvail();
		const ImVec2 vCursor = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(vCursor.x + std::max(0.0f, 0.5f * (vAvailable.x - vButtonSize.x)), vCursor.y + std::max(0.0f, 0.5f * (vAvailable.y - vButtonSize.y))));

		ImGui::BeginDisabled(gpGame->mWeaponModeToggle.IsPending());
		if (ImGui::Button(pLabel, vButtonSize))
		{
			if (gpClientSession != nullptr && gpGame->ClientPlayerId().IsValid())
			{
				gpGame->mWeaponModeToggle.SetPending();
				float fNavigationDelay = rPlayers.pfNavigationDelays[*oPlayerIndex];
				gpClientSession->SendUpdatePlayerRequest(gpGame->ClientPlayerId().iValue, !bUseMissiles, fNavigationDelay);
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
