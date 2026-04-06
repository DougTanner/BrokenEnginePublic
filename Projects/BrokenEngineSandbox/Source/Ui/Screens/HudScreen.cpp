#include "HudScreen.h"

#if defined(BT_CLIENT)

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

	if (gpGame->mGameFlags & engine::GameFlags::kDeathScreen && gpGame->PlayerCount() == 0)
	{
		return;
	}

	auto coordIt = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
	if (coordIt == gpGame->mCoordFrames.end() || coordIt->second.pCurrent == nullptr)
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

	ImGuiIO& rIo = ImGui::GetIO();
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	std::optional<int64_t> oIdx = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
	if (oIdx)
	{
		PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
		RenderBar(pDrawList, rIo.DisplaySize, rPlayers.pfShields[*oIdx], kfShieldHalfWidthPerPoint, -1.0f, kuiShieldColor, mShieldIconVkDescriptorSet);
		RenderBar(pDrawList, rIo.DisplaySize, rPlayers.pfArmors[*oIdx], kfArmorHalfWidthPerPoint, 1.0f, kuiArmorColor, mArmorIconVkDescriptorSet);
	}

	RenderPlayerPanel();
	RenderFocusedPlayerPanel();
}

void HudScreen::RenderPlayerPanel()
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.42f), ImGuiCond_Always);
	ImGui::Begin("PlayerPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	int64_t iPlayerCount = gpGame->PlayerCount();

	// Update spawn toggle: clears pending when player count changes
	gpGame->mSpawnToggle.Update(iPlayerCount);

	// Navigation row: [<] index/count [>] [+]
	bool bCanPrev = gpGame->CanFocusPrev();
	ImGui::BeginDisabled(!bCanPrev);
	if (ImGui::Button("[<]"))
	{
		gpGame->FocusPrev();
		gpClientSession->UpdateDesiredCoords("FocusPrev");
		Log(kVerbose, "HUD FocusPrev NewIndex: {} PlayerCount: {}", gpGame->FocusedPlayerIndex(), iPlayerCount); // DT TEMP
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::Text("%lld/%lld", gpGame->FocusedPlayerIndex() + 1, iPlayerCount);
	ImGui::SameLine();

	bool bCanNext = gpGame->CanFocusNext();
	ImGui::BeginDisabled(!bCanNext);
	if (ImGui::Button("[>]"))
	{
		gpGame->FocusNext();
		gpClientSession->UpdateDesiredCoords("FocusNext");
		Log(kVerbose, "HUD FocusNext NewIndex: {} PlayerCount: {}", gpGame->FocusedPlayerIndex(), iPlayerCount); // DT TEMP
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(gpGame->mSpawnToggle.IsPending());
	if (ImGui::Button("[+]"))
	{
		if (gpClientSession != nullptr)
		{
			gpGame->mSpawnToggle.SetPending();
			engine::gpClient->SendSpawnRequest({engine::ClientRequestFlags::kSpawnRequested});
			Log(kVerbose, "HUD SpawnRequest PlayerCount: {}", iPlayerCount); // DT TEMP
		}
	}
	ImGui::EndDisabled();

	ImGui::End();
}

void HudScreen::RenderFocusedPlayerPanel()
{
	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.95f, rIo.DisplaySize.y * 0.42f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::Begin("FocusedPlayerPanel", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	std::optional<int64_t> playerIndex = std::nullopt;
	{
		auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
		if (it != gpGame->mCoordFrames.end() && it->second.pCurrent != nullptr)
		{
			playerIndex = gpGame->ClientPlayerIndex(*gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers);
		}
	}
	if (playerIndex)
	{
		PlayersPostRender& rPlayers = *gpGame->RenderFrame(gpGame->mClientGridCoord).postRender.pPlayers;
		bool bUseMissiles = static_cast<bool>(rPlayers.pFlags[*playerIndex] & PlayerFlags::kUseMissiles);
		float fNavigationDelay = rPlayers.pfNavigationDelays[*playerIndex];
		gpGame->mWeaponModeToggle.Update(bUseMissiles);
		gpGame->mNavigationDelayControl.Update(fNavigationDelay);

		const char* pLabel = bUseMissiles ? "[Q] Missiles" : "[Q] Blasters";
		ImGui::BeginDisabled(gpGame->mWeaponModeToggle.IsPending());
		if (ImGui::Button(pLabel))
		{
			if (gpClientSession != nullptr && gpGame->ClientPlayerId().IsValid())
			{
				gpGame->mWeaponModeToggle.SetPending();
				engine::gpClient->SendUpdatePlayerRequest(gpGame->ClientPlayerId().iValue, !bUseMissiles, fNavigationDelay);
				Log(kVerbose, "HUD WeaponModeToggle GlobalPlayerId: {}", gpGame->ClientPlayerId().iValue); // DT TEMP
			}
		}
		ImGui::EndDisabled();

		// Navigation delay slider
		ImGui::BeginDisabled(gpGame->mNavigationDelayControl.IsPending());
		float fSliderValue = fNavigationDelay;
		ImGui::SliderFloat("Nav Delay", &fSliderValue, 0.0f, 10.0f);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (gpClientSession != nullptr && gpGame->ClientPlayerId().IsValid())
			{
				gpGame->mNavigationDelayControl.SetPending();
				engine::gpClient->SendUpdatePlayerRequest(gpGame->ClientPlayerId().iValue, bUseMissiles, fSliderValue);
				Log(kVerbose, "HUD NavigationDelay GlobalPlayerId: {} Delay: {}", gpGame->ClientPlayerId().iValue, fSliderValue); // DT TEMP
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
