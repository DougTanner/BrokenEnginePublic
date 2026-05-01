#include "HudScreen.h"

#if defined(BT_CLIENT)

#include "Fleet.h"
#include "Game.h"
#include "Frame/Collections/Players/Players.h"
#include "MenuUtils.h"

namespace
{

constexpr float kfActivationDistancePixels = 350.0f;
constexpr float kfSlideRate = 8.0f;
constexpr float kfEdgeMarginPixels = 8.0f;

} // namespace

namespace game
{

float HudScreen::UpdateSlideAndGetOffsetX(SlidePanelState& rState, ImVec2 vAnchor, float fPivotX, float fSidePivotSign)
{
	ImGuiIO& rIo = ImGui::GetIO();

	// First-frame guard: no real size cached yet. Push by 2x display width so the panel is fully off-screen
	// regardless of the auto-sized width ImGui chooses this frame.
	if (rState.vLastSize.x <= 0.0f)
	{
		return fSidePivotSign * 2.0f * rIo.DisplaySize.x;
	}

	const float fRectMinX = vAnchor.x - fPivotX * rState.vLastSize.x;
	const float fRectMaxX = fRectMinX + rState.vLastSize.x;
	const float fRectMinY = vAnchor.y;
	const float fRectMaxY = vAnchor.y + rState.vLastSize.y;

	const ImVec2 vMouse = rIo.MousePos;
	const float fDx = std::max({fRectMinX - vMouse.x, 0.0f, vMouse.x - fRectMaxX});
	const float fDy = std::max({fRectMinY - vMouse.y, 0.0f, vMouse.y - fRectMaxY});
	const float fDistance = std::sqrt(fDx * fDx + fDy * fDy);

	const float fTarget = 1.0f - std::clamp(fDistance / kfActivationDistancePixels, 0.0f, 1.0f);
	rState.fOpenness += (fTarget - rState.fOpenness) * common::ExponentialInterpolant(kfSlideRate, rIo.DeltaTime);

	// Slide distance must cover both the panel's own width AND the gap from the anchor to the screen edge
	// (panels are anchored at 5%/95%, so 5% of the screen would otherwise still show at openness=0).
	const float fGapToEdge = (fSidePivotSign < 0.0f) ? vAnchor.x : (rIo.DisplaySize.x - vAnchor.x);
	return fSidePivotSign * (1.0f - rState.fOpenness) * (fGapToEdge + rState.vLastSize.x + kfEdgeMarginPixels);
}

void HudScreen::Render()
{
	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	RenderFleetPanel();
	RenderFocusedPlayerPanel();
}

void HudScreen::RenderFleetPanel()
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	const ImVec2 vAnchor(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.42f);
	const float fOffsetX = UpdateSlideAndGetOffsetX(mFleetSlide, vAnchor, 0.0f, -1.0f);
	ImGui::SetNextWindowPos(ImVec2(vAnchor.x + fOffsetX, vAnchor.y), ImGuiCond_Always);
	ImGui::Begin("FleetPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);
	mFleetSlide.vLastSize = ImGui::GetWindowSize();
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
				snprintf(pcLabel, sizeof(pcLabel), "Ship %lld (%d,%d) #%lld", i + 1, memberCoord.x, memberCoord.y, rMember.globalPlayerId.iValue);
				if (ImGui::Selectable(pcLabel, bSelected))
				{
					gpGame->SelectPlayerInFleet(i);
					gpClientSession->UpdateDesiredCoords(SubscriptionChangeReason::kSelectPlayer);
				}
			}
			else
			{
				char pcLabel[64];
				snprintf(pcLabel, sizeof(pcLabel), "Ship %lld [DEAD] #%lld", i + 1, rMember.globalPlayerId.iValue);
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
		static float sNavDelayEditValue = 0.0f;
		float fSliderValue = pFleet->fNavigationDelay;
		if (ImGui::SliderFloat("Nav Delay", &fSliderValue, 0.0f, 10.0f))
		{
			sNavDelayEditValue = fSliderValue;
		}
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (gpClientSession != nullptr)
			{
				gpGame->mNavigationDelayControl.SetPending();
				gpClientSession->SendFleetNavigationDelayRequest(gpGame->FocusedFleetIndex(), sNavDelayEditValue);
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::End();
}

void HudScreen::RenderFocusedPlayerPanel()
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	const ImVec2 vAnchor(rIo.DisplaySize.x * 0.95f, rIo.DisplaySize.y * 0.42f);
	const float fOffsetX = UpdateSlideAndGetOffsetX(mFocusedPlayerSlide, vAnchor, 1.0f, 1.0f);
	ImGui::SetNextWindowPos(ImVec2(vAnchor.x + fOffsetX, vAnchor.y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::Begin("FocusedPlayerPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);
	mFocusedPlayerSlide.vLastSize = ImGui::GetWindowSize();
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	std::optional<int64_t> playerIndex = std::nullopt;
	{
		auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		if (it != gpGame->mCoordFrames.end() && it->second.iSnapshotCount > 0)
		{
			playerIndex = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
		}
	}
	if (playerIndex)
	{
		PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
		bool bUseMissiles = static_cast<bool>(rPlayers.pFlags[*playerIndex] & PlayerFlags::kUseMissiles);
		gpGame->mWeaponModeToggle.Update(bUseMissiles);

		const char* pLabel = bUseMissiles ? "[Q] Missiles" : "[Q] Blasters";
		ImGui::BeginDisabled(gpGame->mWeaponModeToggle.IsPending());
		if (ImGui::Button(pLabel))
		{
			if (gpClientSession != nullptr && gpGame->ClientPlayerId().IsValid())
			{
				gpGame->mWeaponModeToggle.SetPending();
				float fNavigationDelay = rPlayers.pfNavigationDelays[*playerIndex];
				gpClientSession->SendUpdatePlayerRequest(gpGame->ClientPlayerId().iValue, !bUseMissiles, fNavigationDelay);
			}
		}
		ImGui::EndDisabled();
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
