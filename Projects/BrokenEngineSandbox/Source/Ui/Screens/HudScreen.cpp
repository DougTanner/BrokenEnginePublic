#include "HudScreen.h"

#if defined(BT_CLIENT)

#include "Fleet.h"
#include "Game.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Players/Players.h"
#include "MenuUtils.h"

#include "Data/Texture.h"

namespace game
{

// HUD scaling constants
static constexpr float kfUiScale = 0.9f;
static constexpr float kfBottomPadding = kfUiScale * 0.045f;
static constexpr float kfBarSpacing = kfUiScale * 0.028f;
static constexpr float kfIconSize = kfUiScale * 0.025f;
static constexpr float kfBarHeight = kfUiScale * 0.009f;
static constexpr float kfBarCapWidth = kfUiScale * 0.002f;
static constexpr float kfShieldHalfWidthPerPoint = kfUiScale * 0.002f;
static constexpr float kfArmorHalfWidthPerPoint = kfUiScale * 0.004f;

// Colors
static constexpr ImU32 kuiShieldColor = IM_COL32(0x00, 0x88, 0xFF, 0xFF);
static constexpr ImU32 kuiArmorColor = IM_COL32(0xFF, 0x22, 0x22, 0xFF);
static constexpr ImU32 kuiWhiteColor = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);

void HudScreen::Shutdown()
{
	if (mShieldIconVkDescriptorSet != VK_NULL_HANDLE)
	{
		ImGui_ImplVulkan_RemoveTexture(mShieldIconVkDescriptorSet);
		mShieldIconVkDescriptorSet = VK_NULL_HANDLE;
	}
	if (mArmorIconVkDescriptorSet != VK_NULL_HANDLE)
	{
		ImGui_ImplVulkan_RemoveTexture(mArmorIconVkDescriptorSet);
		mArmorIconVkDescriptorSet = VK_NULL_HANDLE;
	}
}

void HudScreen::Initialize()
{
	mShieldIconVkDescriptorSet = VK_NULL_HANDLE;
	mArmorIconVkDescriptorSet = VK_NULL_HANDLE;
	mbTexturesRequested = false;
}

void HudScreen::Render()
{
	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	// Request texture loading on first render (same pattern as Pipeline::WriteIndirectBuffer)
	if (!mbTexturesRequested)
	{
		mbTexturesRequested = true;
		engine::gpFileManager->RequestChunkLoad(std::to_array<common::crc_t>({data::kTexturesUiBC7ShieldIconpngCrc, data::kTexturesUiBC7ArmorIconpngCrc}));
	}

	// Create ImGui descriptors when textures become ready
	if (mShieldIconVkDescriptorSet == VK_NULL_HANDLE && engine::gpFileManager->IsChunkReady(data::kTexturesUiBC7ShieldIconpngCrc))
	{
		mShieldIconVkDescriptorSet = ImGui_ImplVulkan_AddTexture(engine::gpTextureManager->mVkSamplerClamp, engine::gpTextureManager->mTextureMap.at(data::kTexturesUiBC7ShieldIconpngCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	if (mArmorIconVkDescriptorSet == VK_NULL_HANDLE && engine::gpFileManager->IsChunkReady(data::kTexturesUiBC7ArmorIconpngCrc))
	{
		mArmorIconVkDescriptorSet = ImGui_ImplVulkan_AddTexture(engine::gpTextureManager->mVkSamplerClamp, engine::gpTextureManager->mTextureMap.at(data::kTexturesUiBC7ArmorIconpngCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	// Render health bars only if we have a focused player in view
	auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
	if (coordIt != gpGame->mCoordFrames.end() && coordIt->second.iSnapshotCount > 0)
	{
		ImGuiIO& rIo = ImGui::GetIO();
		ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

		std::optional<int64_t> oIdx = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
		if (oIdx)
		{
			PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
			RenderBar(pDrawList, rIo.DisplaySize, rPlayers.pfShields[*oIdx], kfShieldHalfWidthPerPoint, -1.0f, kuiShieldColor, mShieldIconVkDescriptorSet);
			RenderBar(pDrawList, rIo.DisplaySize, rPlayers.pfArmors[*oIdx], kfArmorHalfWidthPerPoint, 1.0f, kuiArmorColor, mArmorIconVkDescriptorSet);
		}
	}

	RenderFleetPanel();
	RenderFocusedPlayerPanel();
}

void HudScreen::RenderFleetPanel()
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.42f), ImGuiCond_Always);
	ImGui::Begin("FleetPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);
	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	int64_t iFleetCount = gpGame->FleetCount();

	// Update fleet toggle: clears pending when fleet count changes
	mCreateFleetToggle.Update(iFleetCount);

	// Fleet navigation row: [<] fleet_index/fleet_count [>] [+]
	ImGui::BeginDisabled(!gpGame->CanFocusPrevFleet());
	if (ImGui::Button("[<]"))
	{
		gpGame->FocusPrevFleet();
		gpClientSession->UpdateDesiredCoords("FocusPrevFleet");
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
		gpClientSession->UpdateDesiredCoords("FocusNextFleet");
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
					gpClientSession->UpdateDesiredCoords("SelectPlayer");
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

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.95f, rIo.DisplaySize.y * 0.42f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::Begin("FocusedPlayerPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);
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

void HudScreen::RenderBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize, float fValue, float fHalfWidthPerPoint, float fBarYSign, ImU32 uiBarColor, VkDescriptorSet vkIconDescriptorSet)
{
	const float fAspectRatio = engine::gpSwapchainManager->mfAspectRatio;

	float fHalfWidth = std::max(fHalfWidthPerPoint * fValue, 0.001f);

	// Calculate position (centered horizontally, near bottom)
	float fCenterX = rDisplaySize.x * 0.5f;
	float fBottomY = rDisplaySize.y * (1.0f - kfBottomPadding);
	float fBarCenterY = fBottomY + fBarYSign * rDisplaySize.y * kfBarSpacing * 0.5f;

	// Convert normalized sizes to pixels
	float fBarHeightPixels = rDisplaySize.y * kfBarHeight;
	float fCapWidth = rDisplaySize.y * kfBarCapWidth;
	float fHalfWidthPixels = rDisplaySize.x * fHalfWidth / fAspectRatio;
	float fIconSizePixels = rDisplaySize.y * kfIconSize;

	// Bar extends both directions from center
	float fBarLeft = fCenterX - fHalfWidthPixels;
	float fBarRight = fCenterX + fHalfWidthPixels;
	float fBarTop = fBarCenterY - fBarHeightPixels * 0.5f;
	float fBarBottom = fBarCenterY + fBarHeightPixels * 0.5f;

	// Left end cap (white)
	pDrawList->AddRectFilled(ImVec2(fBarLeft - fCapWidth, fBarTop), ImVec2(fBarLeft, fBarBottom), kuiWhiteColor);

	// Bar fill
	pDrawList->AddRectFilled(ImVec2(fBarLeft, fBarTop), ImVec2(fBarRight, fBarBottom), uiBarColor);

	// Right end cap (white)
	pDrawList->AddRectFilled(ImVec2(fBarRight, fBarTop), ImVec2(fBarRight + fCapWidth, fBarBottom), kuiWhiteColor);

	// Icon (centered, square)
	if (vkIconDescriptorSet != VK_NULL_HANDLE)
	{
		float fIconLeft = fCenterX - fIconSizePixels * 0.5f;
		float fIconTop = fBarCenterY - fIconSizePixels * 0.5f;
		pDrawList->AddImage(vkIconDescriptorSet, ImVec2(fIconLeft, fIconTop), ImVec2(fIconLeft + fIconSizePixels, fIconTop + fIconSizePixels));
	}
}

} // namespace game

#endif // BT_CLIENT
