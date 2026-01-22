#include "HudScreen.h"

#include "Game.h"
#include "Frame/HealthDamage.h"
#include "Graphics/Graphics.h"

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
	ImGui_ImplVulkan_RemoveTexture(mShieldIconDescriptor);
	ImGui_ImplVulkan_RemoveTexture(mArmorIconDescriptor);
}

void HudScreen::Initialize()
{
	mShieldIconDescriptor = ImGui_ImplVulkan_AddTexture(engine::gpTextureManager->mVkSamplerClamp, engine::gpTextureManager->mTextureMap.at(data::kTexturesUiBC7ShieldIconpngCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	mArmorIconDescriptor = ImGui_ImplVulkan_AddTexture(engine::gpTextureManager->mVkSamplerClamp, engine::gpTextureManager->mTextureMap.at(data::kTexturesUiBC7ArmorIconpngCrc).mVkImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void HudScreen::Render()
{
	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	if (gpGame->CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ImDrawList* pDrawList = ImGui::GetBackgroundDrawList();

	RenderShieldBar(pDrawList, rIo.DisplaySize);
	RenderArmorBar(pDrawList, rIo.DisplaySize);
}

void HudScreen::RenderShieldBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize)
{
	float fAspectRatio = engine::gpSwapchainManager->mfAspectRatio;

	// Get player shield value and calculate half-width (bar extends both directions from center)
	float fShield = gpGame->CurrentFrame().postRender.player.fShield;
	float fHalfWidth = std::max(kfShieldHalfWidthPerPoint * fShield, 0.001f);

	// Calculate position (centered horizontally, near bottom)
	float fCenterX = rDisplaySize.x * 0.5f;
	float fBottomY = rDisplaySize.y * (1.0f - kfBottomPadding);

	// Shield bar is in the upper row of the two-row layout
	float fBarCenterY = fBottomY - rDisplaySize.y * kfBarSpacing * 0.5f;

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

	// Shield bar (blue)
	pDrawList->AddRectFilled(ImVec2(fBarLeft, fBarTop), ImVec2(fBarRight, fBarBottom), kuiShieldColor);

	// Right end cap (white)
	pDrawList->AddRectFilled(ImVec2(fBarRight, fBarTop), ImVec2(fBarRight + fCapWidth, fBarBottom), kuiWhiteColor);

	// Shield icon (centered, square)
	float fIconLeft = fCenterX - fIconSizePixels * 0.5f;
	float fIconTop = fBarCenterY - fIconSizePixels * 0.5f;
	pDrawList->AddImage(mShieldIconDescriptor, ImVec2(fIconLeft, fIconTop), ImVec2(fIconLeft + fIconSizePixels, fIconTop + fIconSizePixels));
}

void HudScreen::RenderArmorBar(ImDrawList* pDrawList, const ImVec2& rDisplaySize)
{
	float fAspectRatio = engine::gpSwapchainManager->mfAspectRatio;

	// Get player armor value and calculate half-width (bar extends both directions from center)
	float fArmor = gpGame->CurrentFrame().postRender.player.fArmor;
	float fHalfWidth = std::max(kfArmorHalfWidthPerPoint * fArmor, 0.001f);

	// Calculate position (centered horizontally, near bottom)
	float fCenterX = rDisplaySize.x * 0.5f;
	float fBottomY = rDisplaySize.y * (1.0f - kfBottomPadding);

	// Armor bar is in the lower row of the two-row layout
	float fBarCenterY = fBottomY + rDisplaySize.y * kfBarSpacing * 0.5f;

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

	// Armor bar (red)
	pDrawList->AddRectFilled(ImVec2(fBarLeft, fBarTop), ImVec2(fBarRight, fBarBottom), kuiArmorColor);

	// Right end cap (white)
	pDrawList->AddRectFilled(ImVec2(fBarRight, fBarTop), ImVec2(fBarRight + fCapWidth, fBarBottom), kuiWhiteColor);

	// Armor icon (centered, square)
	float fIconLeft = fCenterX - fIconSizePixels * 0.5f;
	float fIconTop = fBarCenterY - fIconSizePixels * 0.5f;
	pDrawList->AddImage(mArmorIconDescriptor, ImVec2(fIconLeft, fIconTop), ImVec2(fIconLeft + fIconSizePixels, fIconTop + fIconSizePixels));
}

} // namespace game
