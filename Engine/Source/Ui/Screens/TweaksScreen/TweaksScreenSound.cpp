#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/SoundSettingsWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gSoundRegistrar
{
	{"Master Volume", &gMasterVolume},
	{"Music Volume", &gMusicVolume},
	{"Sound Volume", &gSoundVolume},
	// Listener Distance Start
	{"Listener Distance Start Start Height", &gListenerDistanceStart.StartHeight},
	{"Listener Distance Start End Height", &gListenerDistanceStart.EndHeight},
	{"Listener Distance Start Low", &gListenerDistanceStart.Low},
	{"Listener Distance Start High", &gListenerDistanceStart.High},
	// Listener Distance End
	{"Listener Distance End Start Height", &gListenerDistanceEnd.StartHeight},
	{"Listener Distance End End Height", &gListenerDistanceEnd.EndHeight},
	{"Listener Distance End Low", &gListenerDistanceEnd.Low},
	{"Listener Distance End High", &gListenerDistanceEnd.High},
	// Listener Curve
	{"Listener Curve Start Height", &gListenerCurve.StartHeight},
	{"Listener Curve End Height", &gListenerCurve.EndHeight},
	{"Listener Curve Low", &gListenerCurve.Low},
	{"Listener Curve High", &gListenerCurve.High},
	// Listener Audible Floor
	{"Listener Audible Floor Start Height", &gListenerAudibleFloor.StartHeight},
	{"Listener Audible Floor End Height", &gListenerAudibleFloor.EndHeight},
	{"Listener Audible Floor Low", &gListenerAudibleFloor.Low},
	{"Listener Audible Floor High", &gListenerAudibleFloor.High},
};
}

void TweaksScreenBase::RenderSoundSection()
{
	const int64_t iSection = giTweakSectionSound;

	if (ImGui::BeginTabBar("SoundTabs"))
	{
		if (BeginSubtab("Volumes", iSection, 0))
		{
			// Engine settings rendered inline (no outer table) so the game-side hook can own its own 2-column table at full sub-tab width.
			WrapperSeparatorText("Settings");
			WrapperSlider("Master", iSection, 1.0f, "Master Volume");
			WrapperSlider("Music", iSection, 1.0f, "Music Volume");
			WrapperSlider("Sound", iSection, 1.0f, "Sound Volume");

			RenderSoundEffects();

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Tweaks", iSection, 1))
		{
			WrapperSeparatorText("Listener Distance Start");
			WrapperSlider("Start Height", iSection, 2.0f, "Listener Distance Start Start Height");
			WrapperSlider("End Height", iSection, 2.0f, "Listener Distance Start End Height");
			WrapperSlider("Low", iSection, 2.0f, "Listener Distance Start Low");
			WrapperSlider("High", iSection, 2.0f, "Listener Distance Start High");

			WrapperSeparatorText("Listener Distance End");
			WrapperSlider("Start Height", iSection, 2.0f, "Listener Distance End Start Height");
			WrapperSlider("End Height", iSection, 2.0f, "Listener Distance End End Height");
			WrapperSlider("Low", iSection, 2.0f, "Listener Distance End Low");
			WrapperSlider("High", iSection, 2.0f, "Listener Distance End High");

			WrapperSeparatorText("Listener Curve");
			WrapperSlider("Start Height", iSection, 2.0f, "Listener Curve Start Height");
			WrapperSlider("End Height", iSection, 2.0f, "Listener Curve End Height");
			WrapperSlider("Low", iSection, 2.0f, "Listener Curve Low");
			WrapperSlider("High", iSection, 2.0f, "Listener Curve High");

			WrapperSeparatorText("Listener Audible Floor");
			WrapperSlider("Start Height", iSection, 2.0f, "Listener Audible Floor Start Height");
			WrapperSlider("End Height", iSection, 2.0f, "Listener Audible Floor End Height");
			WrapperSlider("Low", iSection, 2.0f, "Listener Audible Floor Low");
			WrapperSlider("High", iSection, 2.0f, "Listener Audible Floor High");

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
