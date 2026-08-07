#include "SoundMenuScreen.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "MenuUtils.h"
#include "Ui/Localization.h"
#include "Ui/SoundSettingsWrappersBase.h"

namespace game
{

namespace
{

constexpr float kfSoundSliderWidthPixels = 640.0f;

} // namespace

void SoundMenuScreen::Render()
{
	if (gpGame->meUiState != UiState::kSound)
	{
		return;
	}

	ImGuiIO& rIo = ImGui::GetIO();
	ScopedMenuScale menuScale;

	// Centered via the pivot convention (UserInterfaceDesign.txt section 5), matching Pause/Graphics
	ImGui::SetNextWindowPos(ImVec2(rIo.DisplaySize.x * 0.5f, rIo.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ScopedMenuFont menuFont;
	ImGui::Begin("SoundMenu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

	engine::gpImGuiManager->RegisterOpaqueRect(ImGui::GetWindowPos(), ImGui::GetWindowSize());

	// Border + accent strip only — the opaque themed WindowBg must stay intact for RegisterOpaqueRect occlusion
	ImVec2 vPanelPos = ImGui::GetWindowPos();
	ImVec2 vPanelSize = ImGui::GetWindowSize();
	DrawPanelAccents(ImGui::GetWindowDrawList(), vPanelPos, ImVec2(vPanelPos.x + vPanelSize.x, vPanelPos.y + vPanelSize.y));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;

	MenuHeading(AppendUtf8(rWorkbuffer, TranslatedString(kStringAudio)), kfMainMenuHeadingScale);

	const float fSliderWidth = kfSoundSliderWidthPixels * engine::UiScale();
	ImGui::SetNextItemWidth(fSliderWidth);
	WrapperSlider("Master Volume", &engine::gMasterVolume);
	ImGui::SetNextItemWidth(fSliderWidth);
	WrapperSlider("Music Volume", &engine::gMusicVolume);
	ImGui::SetNextItemWidth(fSliderWidth);
	WrapperSlider("Sound Volume", &engine::gSoundVolume);

	ImGui::Separator();

	// One themed width shared by both buttons (measured under the live menu font)
	float fButtonWidth = MenuButtonsWidth({TranslatedString(kStringDefaults), U"Back"});

	// Defaults button
	if (MenuButton(AppendUtf8(rWorkbuffer, TranslatedString(kStringDefaults)), ImVec2(fButtonWidth, 0.0f), mfDefaultsHoverAnim))
	{
		ResetSoundSettings();
	}

	ImGui::SameLine();

	// Back button
	if (MenuButton("Back", ImVec2(fButtonWidth, 0.0f), mfBackHoverAnim))
	{
		gpGame->meUiState = UiState::kPause;
	}

	ImGui::End();
}

} // namespace game

#endif // BT_CLIENT
