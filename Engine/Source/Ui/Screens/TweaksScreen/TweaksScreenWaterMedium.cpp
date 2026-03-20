#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderWaterMediumSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterMedium);

	RenderWaveCountRadioButtons(gMediumCount);

	WrapperSeparatorText("Wave");
	WrapperSlider("Medium Wavelength", kiSection);
	WrapperSlider("Medium Amplitude", kiSection);
	WrapperSlider("Medium Speed", kiSection);
	WrapperSlider("Medium Steepness", kiSection);

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Medium Angle Adjust", kiSection);
	WrapperSlider("Medium Wavelength Adjust", kiSection);
	WrapperSlider("Medium Amplitude Adjust", kiSection);
	WrapperSlider("Medium Speed Adjust", kiSection);
}

} // namespace engine
