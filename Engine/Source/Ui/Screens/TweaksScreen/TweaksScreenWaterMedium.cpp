#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderWaterMediumSection()
{
	// Radio buttons for wave count selection (skip when slider is active)
	if (mActiveSlider.empty())
	{
		ImGui::Text("Wave Count");
		ImGui::SameLine();
		int64_t iCurrent = gMediumCount.Get<int64_t>();
		for (const std::pair<const char*, int64_t>& rPair : std::initializer_list<std::pair<const char*, int64_t>>{{"15", 15}, {"31", 31}, {"63", 63}, {"127", 127}, {"255", 255}})
		{
			if (ImGui::RadioButton(rPair.first, iCurrent == rPair.second))
			{
				gMediumCount.Set(rPair.second);
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	WrapperSeparatorText("Wave");
	WrapperSlider("Medium Wavelength", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Amplitude", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Speed", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Steepness", static_cast<int>(TweakSection::kWaterMedium));

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Medium Angle Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Wavelength Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Amplitude Adjust", static_cast<int>(TweakSection::kWaterMedium));
	WrapperSlider("Medium Speed Adjust", static_cast<int>(TweakSection::kWaterMedium));
}

} // namespace engine
