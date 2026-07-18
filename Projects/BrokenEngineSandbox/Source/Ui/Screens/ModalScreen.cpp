#include "ModalScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"

namespace game
{

namespace
{

constexpr float kfModalWindowWidthFraction = 0.24f;
constexpr float kfModalMessageFontScale = 1.15f;
constexpr float kfModalMessageActionGapPixels = 36.0f;

} // namespace

void ModalScreen::Render()
{
	if (gpGame->meUiState != UiState::kModal)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	float fWindowWidth = rIo.DisplaySize.x * kfModalWindowWidthFraction;
	// Pivot-centered horizontally, seated slightly above center (UserInterfaceDesign.txt section 5)
	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * kfModalAnchorFractionY), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(fWindowWidth, 0.0f));

	ScopedMenuFont menuFont;
	ImGui::Begin("ModalDialog", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// Border + accent strip only — the opaque themed WindowBg must stay intact for RegisterOpaqueRect occlusion
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	{
		ScopedMenuFont messageFont(kfMenuUiScale * kfModalMessageFontScale);
		ImGui::TextWrapped("%s", gpGame->mModalMessage);
	}

	ImGui::Dummy(ImVec2(0.0f, kfModalMessageActionGapPixels * engine::UiScale()));

	// Text-driven width, floored to the modal button minimum; centered in the window via the measured width
	float fButtonWidth = std::max(MenuButtonsWidth({U"OK"}), kfModalButtonMinWidthPixels * engine::UiScale());
	ImGui::SetCursorPosX((fWindowWidth - fButtonWidth) / 2.0f);
	if (MenuButton("OK", ImVec2(fButtonWidth, 0.0f), mfOkHoverAnim))
	{
		gpGame->meUiState = UiState::kPause;
		gpGame->mModalMessage[0] = '\0';
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
