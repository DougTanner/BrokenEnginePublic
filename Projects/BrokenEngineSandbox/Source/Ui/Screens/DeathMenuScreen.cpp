#include "DeathMenuScreen.h"

#include "Game.h"
#include "MenuHelpers.h"
#include "Ui/Localization.h"

namespace game
{

void DeathMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kNone)
	{
		return;
	}

	if (!(gpGame->CurrentFrame().interpolate.flags & FrameFlags::kDeathScreen))
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Transparent window background
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	float fWindowWidth = rIo.DisplaySize.x * 0.5f;
	ImGui::SetNextWindowPos(ImVec2((rIo.DisplaySize.x - fWindowWidth) / 2.0f, rIo.DisplaySize.y * 0.1f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(fWindowWidth, 0.0f));

	ImGui::Begin("DeathMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale * 1.5f);

	// Game Over text
	std::string gameOverText = ToUtf8(TranslatedString(kStringGameOver));
	float fTextWidth = ImGui::CalcTextSize(gameOverText.c_str()).x;
	ImGui::SetCursorPosX((fWindowWidth - fTextWidth) / 2.0f);
	ImGui::TextUnformatted(gameOverText.c_str());

	ImGui::SetWindowFontScale(kfMenuUiScale);

	ImGui::Dummy(ImVec2(0.0f, rIo.DisplaySize.y * 0.05f));

	// Restart button (centered)
	std::string restartText = ToUtf8(TranslatedString(kStringRestart));
	float fButtonWidth = rIo.DisplaySize.x * 0.2f;
	ImGui::SetCursorPosX((fWindowWidth - fButtonWidth) / 2.0f);
	if (ImGui::Button(restartText.c_str(), ImVec2(fButtonWidth, rIo.DisplaySize.y * 0.045f)))
	{
		gpGame->RemoveAutosave();
		gpGame->ChangeFrame(FrameFlags::kMainMenu);
	}

	ImGui::End();

	ImGui::PopStyleColor(2);
}

} // namespace game
