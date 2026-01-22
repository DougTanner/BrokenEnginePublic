#include "PauseMenuScreen.h"

#include "Game.h"
#include "MenuHelpers.h"
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

	// Get button text strings
	std::string resumeText = ToUtf8(TranslatedString(kStringResume));
	std::string restartText = ToUtf8(TranslatedString(kStringRestart));
	std::string graphicsText = ToUtf8(TranslatedString(kStringGraphics));
	std::string soundText = ToUtf8(TranslatedString(kStringSound));
	std::string mainMenuText = ToUtf8(TranslatedString(kStringMainMenu));
	std::string quitText = ToUtf8(TranslatedString(kStringQuit));

	// Calculate max button width
	float fButtonWidth = ImGui::CalcTextSize(resumeText.c_str()).x;
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(restartText.c_str()).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(graphicsText.c_str()).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(soundText.c_str()).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(mainMenuText.c_str()).x);
	fButtonWidth = std::max(fButtonWidth, ImGui::CalcTextSize(quitText.c_str()).x);
	fButtonWidth += ImGui::GetStyle().FramePadding.x * 2.0f;

	if (ImGui::Button(resumeText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kNone;
	}

	if (ImGui::Button(restartText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->RemoveAutosave();
		gpGame->ChangeFrame(FrameFlags::kMainMenu);
		gpGame->ChangeFrame(FrameFlags::kGame);
		gpGame->meUiState = UiState::kNone;
	}

	if (ImGui::Button(graphicsText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kGraphics;
	}

	if (ImGui::Button(soundText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->meUiState = UiState::kSound;
	}

	if (ImGui::Button(mainMenuText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->mbSavedFrame = true;
		gpGame->ChangeFrame(FrameFlags::kMainMenu);
		gpGame->meUiState = UiState::kPause;
	}

	if (ImGui::Button(quitText.c_str(), ImVec2(fButtonWidth, 0.0f)))
	{
		gpGame->mGameFlags.Set(engine::GameFlags::kQuit);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game
