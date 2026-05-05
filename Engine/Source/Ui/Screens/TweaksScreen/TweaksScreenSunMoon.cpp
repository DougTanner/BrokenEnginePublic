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

	WrapperSeparatorText("Sun Intensity");
	WrapperSlider("Sun Terrain", kiSection);
	WrapperSlider("Sun Water", kiSection);
	WrapperSlider("Sun Objects", kiSection);
	WrapperSlider("Sun Smoke", kiSection);

	WrapperSeparatorText("Moon Intensity");
	WrapperSlider("Moon Terrain", kiSection);
	WrapperSlider("Moon Water", kiSection);
	WrapperSlider("Moon Objects", kiSection);
	WrapperSlider("Moon Smoke", kiSection);
	WrapperSlider("Moon Blue Tint", kiSection);

	WrapperSeparatorText("Moon Timing (radians)");
	WrapperSlider("Moonrise Start", kiSection);
	WrapperSlider("Moonrise End", kiSection);
	WrapperSlider("Moonset Start", kiSection);
	WrapperSlider("Moonset End", kiSection);

	WrapperSeparatorText("Ambient");
	WrapperSlider("Minimum Ambient", kiSection);
	WrapperSlider("Ambient Multiplier", kiSection);

	WrapperSeparatorText("Shadow Night-Gate (radians)");
	WrapperSlider("Shadow Night Multiplier", kiSection);
	WrapperSlider("Shadow Sunset Start", kiSection);
	WrapperSlider("Shadow Sunset End", kiSection);
	WrapperSlider("Shadow Sunrise Start", kiSection);
	WrapperSlider("Shadow Sunrise End", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
