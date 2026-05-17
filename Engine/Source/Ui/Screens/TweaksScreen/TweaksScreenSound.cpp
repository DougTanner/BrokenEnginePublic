#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/SoundSettingsWrappersBase.h"

#if defined(BT_CLIENT)

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
	{"Listener Distance Start Start Height", &gListenerDistanceStartStartHeight},
	{"Listener Distance Start End Height", &gListenerDistanceStartEndHeight},
	{"Listener Distance Start Low", &gListenerDistanceStartLow},
	{"Listener Distance Start High", &gListenerDistanceStartHigh},
	// Listener Distance End
	{"Listener Distance End Start Height", &gListenerDistanceEndStartHeight},
	{"Listener Distance End End Height", &gListenerDistanceEndEndHeight},
	{"Listener Distance End Low", &gListenerDistanceEndLow},
	{"Listener Distance End High", &gListenerDistanceEndHigh},
	// Listener Curve
	{"Listener Curve Start Height", &gListenerCurveStartHeight},
	{"Listener Curve End Height", &gListenerCurveEndHeight},
	{"Listener Curve Low", &gListenerCurveLow},
	{"Listener Curve High", &gListenerCurveHigh},
	// Listener Audible Floor
	{"Listener Audible Floor Start Height", &gListenerAudibleFloorStartHeight},
	{"Listener Audible Floor End Height", &gListenerAudibleFloorEndHeight},
	{"Listener Audible Floor Low", &gListenerAudibleFloorLow},
	{"Listener Audible Floor High", &gListenerAudibleFloorHigh},
};
}

void TweaksScreenBase::RenderSoundSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSound);

	if (ImGui::BeginTabBar("SoundTabs"))
	{
		if (ImGui::BeginTabItem("Volumes", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 0;
			}

			// Engine settings rendered inline (no outer table) so the game-side hook can own its own 2-column table at full sub-tab width.
			WrapperSeparatorText("Settings");
			WrapperSlider("Master", kiSection, 1.0f, "Master Volume");
			WrapperSlider("Music", kiSection, 1.0f, "Music Volume");
			WrapperSlider("Sound", kiSection, 1.0f, "Sound Volume");

			RenderSoundEffects();

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Tweaks", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 1;
			}

			WrapperSeparatorText("Listener Distance Start");
			WrapperSlider("Start Height", kiSection, 2.0f, "Listener Distance Start Start Height");
			WrapperSlider("End Height", kiSection, 2.0f, "Listener Distance Start End Height");
			WrapperSlider("Low", kiSection, 2.0f, "Listener Distance Start Low");
			WrapperSlider("High", kiSection, 2.0f, "Listener Distance Start High");

			WrapperSeparatorText("Listener Distance End");
			WrapperSlider("Start Height", kiSection, 2.0f, "Listener Distance End Start Height");
			WrapperSlider("End Height", kiSection, 2.0f, "Listener Distance End End Height");
			WrapperSlider("Low", kiSection, 2.0f, "Listener Distance End Low");
			WrapperSlider("High", kiSection, 2.0f, "Listener Distance End High");

			WrapperSeparatorText("Listener Curve");
			WrapperSlider("Start Height", kiSection, 2.0f, "Listener Curve Start Height");
			WrapperSlider("End Height", kiSection, 2.0f, "Listener Curve End Height");
			WrapperSlider("Low", kiSection, 2.0f, "Listener Curve Low");
			WrapperSlider("High", kiSection, 2.0f, "Listener Curve High");

			WrapperSeparatorText("Listener Audible Floor");
			WrapperSlider("Start Height", kiSection, 2.0f, "Listener Audible Floor Start Height");
			WrapperSlider("End Height", kiSection, 2.0f, "Listener Audible Floor End Height");
			WrapperSlider("Low", kiSection, 2.0f, "Listener Audible Floor Low");
			WrapperSlider("High", kiSection, 2.0f, "Listener Audible Floor High");

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
