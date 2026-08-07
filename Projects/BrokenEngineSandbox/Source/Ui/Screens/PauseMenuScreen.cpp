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

	// Invisible window lets the pause actions float directly over the dimmed scene
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Center window on screen
	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ScopedMenuFont menuFont;
	ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	// Share the Main Menu's restrained title scale and primary action tier.
	float fButtonWidth = std::max(MenuButtonsWidth({TranslatedString(kStringResume), TranslatedString(kStringGraphics), TranslatedString(kStringAudio), TranslatedString(kStringGameSettings), TranslatedString(kStringMainMenu), TranslatedString(kStringQuit)}), kfPrimaryButtonMinWidthPixels * engine::UiScale());
	float fHeadingWidth = 0.0f;
	{
		ScopedMenuFont headingFont(kfMenuUiScale * kfMainMenuHeadingScale);
		fHeadingWidth = ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringPaused))).x;
	}

	float fContentStartX = ImGui::GetCursorPosX();
	ImGui::SetCursorPosX(fContentStartX + (fButtonWidth - fHeadingWidth) * 0.5f);
	MenuHeading(AppendUtf8(rWorkbuffer, TranslatedString(kStringPaused)), kfMainMenuHeadingScale);
	ImGui::SetCursorPosX(fContentStartX);

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringResume)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[0]))
	{
		gpGame->meUiState = UiState::kNone;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[1]))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringAudio)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[2]))
	{
		gpGame->meUiState = UiState::kSound;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringGameSettings)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[3]))
	{
		gpGame->meUiState = UiState::kGameSettings;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringMainMenu)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[4]))
	{
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = UiState::kPause;
	}

	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, 0.0f), mfButtonHoverAnims[5]))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game

#endif // BT_CLIENT
