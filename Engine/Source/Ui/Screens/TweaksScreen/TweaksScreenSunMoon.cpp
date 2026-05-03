#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

namespace engine
{

void TweaksScreenBase::RenderSunMoonSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kSunMoon);

	WrapperSeparatorText("Color Phase Boundaries (radians)");
	WrapperSlider("Morning Start", kiSection);
	WrapperSlider("Noon Start", kiSection);
	WrapperSlider("Noon End", kiSection);
	WrapperSlider("Evening Start", kiSection);
	WrapperSlider("Night Start", kiSection);

	WrapperSeparatorText("Moon");
	WrapperSlider("Moon Brightness", kiSection);
	WrapperSlider("Moon Blue Tint", kiSection);
	WrapperSlider("Water Moon Brightness", kiSection);

	WrapperSeparatorText("Ambient");
	WrapperSlider("Minimum Ambient", kiSection);

	WrapperSeparatorText("Shadow Night-Gate (radians)");
	WrapperSlider("Shadow Night Multiplier", kiSection);
	WrapperSlider("Shadow Sunset Start", kiSection);
	WrapperSlider("Shadow Sunset End", kiSection);
	WrapperSlider("Shadow Sunrise Start", kiSection);
	WrapperSlider("Shadow Sunrise End", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
