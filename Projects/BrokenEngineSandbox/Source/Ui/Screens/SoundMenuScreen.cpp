#include "SoundMenuScreen.h"

#include "Game.h"
#include "MenuHelpers.h"
#include "Ui/Localization.h"

namespace game
{

void SoundMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kSound)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Semi-transparent background
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.04f), ImGuiCond_Always);

	ImGui::Begin("SoundMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	WrapperSlider("Master Volume", &engine::gMasterVolume);
	WrapperSlider("Music Volume", &engine::gMusicVolume);
	WrapperSlider("Sound Volume", &engine::gSoundVolume);

	ImGui::Separator();

	// Defaults button
	std::string defaultsText = ToUtf8(TranslatedString(kStringDefaults));
	if (ImGui::Button(defaultsText.c_str()))
	{
		Game::ResetSoundSettings();
	}

	ImGui::SameLine();

	// Back button
	if (ImGui::Button("Back"))
	{
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();

	ImGui::PopStyleColor();
}

} // namespace game
