#include "SoundMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/Localization.h"
#include "Ui/SoundSettingsWrappersBase.h"

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

	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.05f, rIo.DisplaySize.y * 0.04f), ImGuiCond_Always);

	ImGui::Begin("SoundMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::SetWindowFontScale(kfMenuUiScale);

	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	WrapperSlider("Master Volume", &engine::gMasterVolume);
	WrapperSlider("Music Volume", &engine::gMusicVolume);
	WrapperSlider("Sound Volume", &engine::gSoundVolume);

	ImGui::Separator();

	// Defaults button
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	if (ImGui::Button(AppendUtf8(rWorkbuffer, TranslatedString(kStringDefaults))))
	{
		ResetSoundSettings();
	}

	ImGui::SameLine();

	// Back button
	if (ImGui::Button("Back"))
	{
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
