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

	// Transparent background
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	// Center window on screen
	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGui::Begin("PauseMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	// Calculate max button width
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	float fButtonWidth = ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringResume))).x;
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringMainMenu))).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit))).x);
	fButtonWidth += ImGui::GetStyle().FramePadding.x * 2.0f;

	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringResume)), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kNone;
	}

	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringGraphics)), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kGraphicsSettings;
	}

	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringSound)), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kSound;
	}

	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringMainMenu)), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = UiState::kPause;
	}

	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringQuit)), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game

#endif // BT_CLIENT
