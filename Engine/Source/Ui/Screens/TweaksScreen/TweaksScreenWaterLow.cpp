#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderWaterLowSection()
{
	// Radio buttons for wave count selection (skip when slider is active)
	if (mActiveSlider.empty())
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gLowCount.Get<int64_t>();
		for (const std::pair<const char*, int64_t>& rPair : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(rPair.first, iCurrent == rPair.second))
			{
				gLowCount.Set(rPair.second);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	WrapperSeparatorText("Wave");
	WrapperSlider("Low Max", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Angle", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Steepness", static_cast<int>(TweakSection::kWaterLow));

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Angle Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Wavelength Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Amplitude Adjust", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Speed Adjust", static_cast<int>(TweakSection::kWaterLow));

	WrapperSeparatorText("Beach Fade");
	WrapperSlider("Beach Directional Fade Bottom", static_cast<int>(TweakSection::kWaterLow));
	WrapperSlider("Beach Directional Fade Height", static_cast<int>(TweakSection::kWaterLow));
}

} // namespace engine
