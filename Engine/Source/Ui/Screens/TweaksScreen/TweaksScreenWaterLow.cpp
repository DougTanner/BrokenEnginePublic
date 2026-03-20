#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderWaterLowSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterLow);

	RenderWaveCountRadioButtons(gLowCount);

	WrapperSeparatorText("Wave");
	WrapperSlider("Low Max", kiSection);
	WrapperSlider("Angle", kiSection);
	WrapperSlider("Wavelength", kiSection);
	WrapperSlider("Amplitude", kiSection);
	WrapperSlider("Speed", kiSection);
	WrapperSlider("Steepness", kiSection);

	WrapperSeparatorText("Adjustments");
	WrapperSlider("Angle Adjust", kiSection);
	WrapperSlider("Wavelength Adjust", kiSection);
	WrapperSlider("Amplitude Adjust", kiSection);
	WrapperSlider("Speed Adjust", kiSection);

	WrapperSeparatorText("Beach Fade");
	WrapperSlider("Beach Directional Fade Bottom", kiSection);
	WrapperSlider("Beach Directional Fade Height", kiSection);
}

} // namespace engine
