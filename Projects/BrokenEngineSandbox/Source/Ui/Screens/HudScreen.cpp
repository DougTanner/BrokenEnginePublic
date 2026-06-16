#include "HudScreen.h"

#if defined(BT_CLIENT)

#include "Fleet.h"
#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "MenuUtils.h"

namespace
{

constexpr float kfActivationDistancePixels = 350.0f;
constexpr float kfSlideRate = 8.0f;
constexpr float kfForceOpenGracePeriodSeconds = 2.0f;

} // namespace

namespace game
{

float HudScreen::ComputeMouseOpennessTarget(ImVec2 vLastSize, ImVec2 vAnchor, float fPivotX)
{
	if (vLastSize.x <= 0.0f)
	{
		return 0.0f;
	}

	const float fRectMinX = vAnchor.x - fPivotX * vLastSize.x;
	const float fRectMaxX = fRectMinX + vLastSize.x;
	const float fRectMinY = vAnchor.y;
	const float fRectMaxY = vAnchor.y + vLastSize.y;

	const ImVec2 vMouse = ImGui::GetIO().MousePos;
	const float fDx = std::max({fRectMinX - vMouse.x, 0.0f, vMouse.x - fRectMaxX});
	const float fDy = std::max({fRectMinY - vMouse.y, 0.0f, vMouse.y - fRectMaxY});
	const float fDistance = std::sqrt(fDx * fDx + fDy * fDy);

	return 1.0f - std::clamp(fDistance / kfActivationDistancePixels, 0.0f, 1.0f);
}

float HudScreen::UpdateSlideAndGetEdgeX(SlidePanelState& rState, ImVec2 vAnchor, float fSidePivotSign, float fTarget)
{
	ImGuiIO& rIo = ImGui::GetIO();

	// Caller must set SetNextWindowPos pivot so the returned x IS the panel's off-screen edge:
	//   right panel (fSidePivotSign > 0): pivot (0.0, 0) — returned x is the left edge
	//   left  panel (fSidePivotSign < 0): pivot (1.0, 0) — returned x is the right edge
	// At openness=0 the off-screen edge is fixed at exactly 1 pixel beyond the screen boundary,
	// width-independent — ImGui auto-resize cannot pull the window back onscreen.
	const float fOffscreenX = (fSidePivotSign > 0.0f) ? rIo.DisplaySize.x + 1.0f : -1.0f;

	if (rState.vLastSize.x <= 0.0f)
	{
		return fOffscreenX;
	}

	rState.fOpenness += (fTarget - rState.fOpenness) * common::ExponentialInterpolant(kfSlideRate, rIo.DeltaTime);

	// At openness=1 the on-screen anchor edge sits at vAnchor.x (offset by size to flip pivot side).
	const float fOnscreenX = (fSidePivotSign > 0.0f) ? (vAnchor.x - rState.vLastSize.x) : (vAnchor.x + rState.vLastSize.x);
	return std::lerp(fOffscreenX, fOnscreenX, rState.fOpenness);
}

void HudScreen::Render()
{
	// Ensure tick prefix for the auto-unhide log — Render() runs outside ClientUpdate's LogTickScope.
	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
		optionalTickScope.emplace(gpGame->TickCounter());

	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	// Force-open the fleet panel when the focused fleet has no presence in any subscribed frame.
	// Iterating all subscribed frames (not just mClientGridCoord) tolerates cell-boundary crossings,
	// where the player's snapshot has migrated to a neighbor before mClientGridCoord catches up.
	bool bWantsForceOpen = false;
	const char* pcWantReason = "fleet member present";
	int iSubscribedFrameCount = 0;

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
	// kWarning so the keLogLevelDefault threshold (kWarning) lets it through.
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
	// Y anchor centers a 75%-tall panel: top = (1 - 0.75) / 2 = 0.125.
	const ImVec2 vLeftAnchor(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.125f);
	const ImVec2 vRightAnchor(rIo.DisplaySize.x * 0.95f, rIo.DisplaySize.y * 0.125f);
	const float fMouseLeft = ComputeMouseOpennessTarget(mFleetSlide.vLastSize, vLeftAnchor, 0.0f);
	const float fMouseRight = ComputeMouseOpennessTarget(mFocusedPlayerSlide.vLastSize, vRightAnchor, 1.0f);
	const float fMouseTarget = std::max(fMouseLeft, fMouseRight);

	// Final shared targets. When right has content: both panels see max(force, mouse) — strict sync.
	// When right has no content: left can still auto-un-hide (force only), right stays hidden.
	const float fForceTarget = bForceOpen ? 1.0f : 0.0f;
	const float fLeftTarget = bRightHasContent ? std::max(fForceTarget, fMouseTarget) : fForceTarget;
	const float fRightTarget = bRightHasContent ? std::max(fForceTarget, fMouseTarget) : 0.0f;

	RenderFleetPanel(fLeftTarget);
	RenderFocusedPlayerPanel(fRightTarget);
}

void HudScreen::RenderFleetPanel(float fTarget)
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	const ImVec2 vAnchor(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.125f);
	const float fEdgeX = UpdateSlideAndGetEdgeX(mFleetSlide, vAnchor, -1.0f, fTarget);
	ImGuiWindowFlags eFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	if (mfFixedPanelWidth > 0.0f)
	{
		ImGui::SetNextWindowSize(ImVec2(mfFixedPanelWidth, rIo.DisplaySize.y * 0.75f), ImGuiCond_Always);
	}
	else
	{
		// First frame baseline capture: auto-size to natural content width.
		eFlags |= ImGuiWindowFlags_AlwaysAutoResize;
	}
	ImGui::SetNextWindowPos(ImVec2(fEdgeX, vAnchor.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::Begin("FleetPanel", nullptr, eFlags);
	ImGui::SetWindowFontScale(kfMenuUiScale);
	mFleetSlide.vLastSize = ImGui::GetWindowSize();
	if (mfFixedPanelWidth <= 0.0f && mFleetSlide.vLastSize.x > 0.0f)
	{
		mfFixedPanelWidth = mFleetSlide.vLastSize.x * 7.2f;
	}
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

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
		if (gpClientSession != nullptr)
		{
			mDeleteFleetToggle.SetPending();
			gpClientSession->SendDeleteFleetRequest(gpGame->FocusedFleetIndex());
			LOG(kDefault, kVerbose, "HUD DeleteFleetRequest Fleet: {} FleetCount: {}", gpGame->FocusedFleetIndex(), iFleetCount);
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
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
				if (ImGui::Selectable(pcLabel, false))
				{
					if (gpClientSession != nullptr)
					{
						gpClientSession->SendRespawnInFleetRequest(gpGame->FocusedFleetIndex(), i);
						LOG(kDefault, kVerbose, "HUD RespawnInFleet Fleet: {} Member: {}", gpGame->FocusedFleetIndex(), i);
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
				gpClientSession->SendSpawnIntoFleetRequest(gpGame->FocusedFleetIndex());
				LOG(kDefault, kVerbose, "HUD SpawnIntoFleet Fleet: {}", gpGame->FocusedFleetIndex());
			}
		}
		ImGui::EndDisabled();

		// Fleet navigation delay slider
		ImGui::Separator();
		gpGame->mNavigationDelayControl.Update(pFleet->fNavigationDelay);
		ImGui::BeginDisabled(gpGame->mNavigationDelayControl.IsPending());
		static float sfNavigationDelayEditValue = 0.0f;
		float fSliderValue = pFleet->fNavigationDelay;
		if (ImGui::SliderFloat("Nav Delay", &fSliderValue, 0.0f, 60.0f))
		{
			sfNavigationDelayEditValue = fSliderValue;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (gpClientSession != nullptr)
			{
				gpGame->mNavigationDelayControl.SetPending();
				gpClientSession->SendFleetNavigationDelayRequest(gpGame->FocusedFleetIndex(), sfNavigationDelayEditValue);
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

	const ImVec2 vAnchor(rIo.DisplaySize.x * 0.95f, rIo.DisplaySize.y * 0.125f);
	const float fEdgeX = UpdateSlideAndGetEdgeX(mFocusedPlayerSlide, vAnchor, 1.0f, fTarget);
	ImGuiWindowFlags eFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
	if (mfFixedPanelWidth > 0.0f)
	{
		ImGui::SetNextWindowSize(ImVec2(mfFixedPanelWidth, rIo.DisplaySize.y * 0.75f), ImGuiCond_Always);
	}
	else
	{
		// Left panel hasn't captured baseline yet — fall back to auto-size for one frame.
		eFlags |= ImGuiWindowFlags_AlwaysAutoResize;
	}
	ImGui::SetNextWindowPos(ImVec2(fEdgeX, vAnchor.y), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
	ImGui::Begin("FocusedPlayerPanel", nullptr, eFlags);
	ImGui::SetWindowFontScale(kfMenuUiScale);
	mFocusedPlayerSlide.vLastSize = ImGui::GetWindowSize();
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	if (oPlayerIndex.has_value())
	{
		PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
		bool bUseMissiles = static_cast<bool>(rPlayers.pFlags[*oPlayerIndex] & PlayerFlags::kUseMissiles);
		gpGame->mWeaponModeToggle.Update(bUseMissiles);

		const char* pLabel = bUseMissiles ? "[Q] Missiles" : "[Q] Blasters";
		ImGui::BeginDisabled(gpGame->mWeaponModeToggle.IsPending());
		if (ImGui::Button(pLabel))
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
