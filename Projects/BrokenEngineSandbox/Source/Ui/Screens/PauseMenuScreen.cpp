#include "PauseMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/Localization.h"

namespace game
{

void PauseMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kPause || gpGame->InMainMenu())
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Dim the game scene behind the pause overlay
	DrawFullScreenDim();

	// Transparent background (panel chrome is custom-drawn below)
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Center window on screen
	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ScopedMenuFont menuFont;
	ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	// AlwaysAutoResize yields last-frame size — accepted one-frame lag (same pattern as HudScreen vLastSize)
	ImVec2 vWindowPos = ImGui::GetWindowPos();
	ImVec2 vWindowSize = ImGui::GetWindowSize();
	DrawPanelBackground(ImGui::GetWindowDrawList(), vWindowPos, ImVec2(vWindowPos.x + vWindowSize.x, vWindowPos.y + vWindowSize.y));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	{
		ScopedMenuFont headingFont(kfMenuUiScale * kfMenuHeadingScale);
		ImGui::TextUnformatted(AppendUtf8(rWorkbuffer, TranslatedString(kStringPaused)));
	}
	ImGui::Dummy(ImVec2(0.0f, rIo.DisplaySize.y * 0.01f));

	// Calculate max button width (measured under the same font that renders)
	float fButtonWidth = ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringResume))).x;
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringMainMenu))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit))).x);
	fButtonWidth += ImGui::GetStyle().FramePadding.x * 4.0f;

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringResume)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]))
	{
		gpGame->meUiState = UiState::kNone;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[1]))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[2]))
	{
		gpGame->meUiState = UiState::kSound;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringMainMenu)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[3]))
	{
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = UiState::kPause;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[4]))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game

#endif // BT_CLIENT
