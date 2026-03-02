#include "ModalScreen.h"

#ifdef BT_CLIENT

#include "Game.h"
#include "MenuUtils.h"

namespace game
{

void ModalScreen::Render()
{
	if (gpGame->meUiState != UiState::kModal)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.05f, 0.85f));

	float fWindowWidth = rIo.DisplaySize.x * 0.35f;
	ImGui::SetNextWindowPos(ImVec2((rIo.DisplaySize.x - fWindowWidth) / 2.0f, rIo.DisplaySize.y * 0.35f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(fWindowWidth, 0.0f));

	ImGui::Begin("ModalDialog", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	ImGui::TextWrapped("%s", gpGame->mModalMessage);

	ImGui::Dummy(ImVec2(0.0f, rIo.DisplaySize.y * 0.03f));

	float fButtonWidth = rIo.DisplaySize.x * 0.1f;
	float fButtonHeight = rIo.DisplaySize.y * 0.04f;
	ImGui::SetCursorPosX((fWindowWidth - fButtonWidth) / 2.0f);
	if (ImGui::Button("OK", ImVec2(fButtonWidth, fButtonHeight)))
	{
		gpGame->meUiState = UiState::kPause;
		gpGame->mModalMessage[0] = '\0';
	}

	ImGui::End();
	ImGui::PopStyleColor();
}

} // namespace game

#endif // BT_CLIENT
