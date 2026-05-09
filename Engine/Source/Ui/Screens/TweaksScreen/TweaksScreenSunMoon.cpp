#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/SunMoonWrappersBase.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gSunMoonRegistrar
{
	// Color Phase Boundaries (radians)
	{"Morning Start", &gSunMoonMorning},
	{"Noon Start", &gSunMoonNoonStart},
	{"Noon End", &gSunMoonNoonEnd},
	{"Evening Start", &gSunMoonEvening},
	{"Night Start", &gSunMoonNightStart},
	// Sun Intensity
	{"Sun Terrain", &gSunMoonSunIntensityTerrain},
	{"Sun Water", &gSunMoonSunIntensityWater},
	{"Sun Objects", &gSunMoonSunIntensityObjects},
	{"Sun Smoke", &gSunMoonSunIntensitySmoke},
	// Moon Intensity + Blue Tint
	{"Moon Terrain", &gSunMoonMoonIntensityTerrain},
	{"Moon Water", &gSunMoonMoonIntensityWater},
	{"Moon Objects", &gSunMoonMoonIntensityObjects},
	{"Moon Smoke", &gSunMoonMoonIntensitySmoke},
	{"Moon Blue Tint", &gSunMoonMoonBlueTint},
	// Moon Timing
	{"Moonrise Start", &gSunMoonMoonriseStart},
	{"Moonrise End", &gSunMoonMoonriseEnd},
	{"Moonset Start", &gSunMoonMoonsetStart},
	{"Moonset End", &gSunMoonMoonsetEnd},
	// Ambient
	{"Minimum Ambient", &gSunMoonMinimumAmbient},
	{"Ambient Multiplier", &gSunMoonAmbientMultiplier},
	// Shadow Night-Gate
	{"Shadow Night Multiplier", &gSunMoonShadowNightMultiplier},
	{"Shadow Sunset Start", &gSunMoonShadowSunsetStart},
	{"Shadow Sunset End", &gSunMoonShadowSunsetEnd},
	{"Shadow Sunrise Start", &gSunMoonShadowSunriseStart},
	{"Shadow Sunrise End", &gSunMoonShadowSunriseEnd},
};
}

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
