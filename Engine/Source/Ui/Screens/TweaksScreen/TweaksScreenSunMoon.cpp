#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/SunMoonWrappersBase.h"

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
	// Normal Tilt
	{"Normal Tilt", &gSunMoonNormalTilt},
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
	const int64_t iSection = giTweakSectionSunMoon;

	WrapperSeparatorText("Color Phase Boundaries (radians)");
	WrapperSlider("Morning Start", iSection);
	WrapperSlider("Noon Start", iSection);
	WrapperSlider("Noon End", iSection);
	WrapperSlider("Evening Start", iSection);
	WrapperSlider("Night Start", iSection);

	WrapperSeparatorText("Sun Intensity");
	WrapperSlider("Sun Terrain", iSection);
	WrapperSlider("Sun Water", iSection);
	WrapperSlider("Sun Objects", iSection);
	WrapperSlider("Sun Smoke", iSection);

	WrapperSeparatorText("Moon Intensity");
	WrapperSlider("Moon Terrain", iSection);
	WrapperSlider("Moon Water", iSection);
	WrapperSlider("Moon Objects", iSection);
	WrapperSlider("Moon Smoke", iSection);
	WrapperSlider("Moon Blue Tint", iSection);

	WrapperSeparatorText("Moon Timing (radians)");
	WrapperSlider("Moonrise Start", iSection);
	WrapperSlider("Moonrise End", iSection);
	WrapperSlider("Moonset Start", iSection);
	WrapperSlider("Moonset End", iSection);

	WrapperSeparatorText("Ambient");
	WrapperSlider("Minimum Ambient", iSection);
	WrapperSlider("Ambient Multiplier", iSection);

	WrapperSeparatorText("Normal Tilt (radians)");
	WrapperSlider("Normal Tilt", iSection);

	WrapperSeparatorText("Shadow Night-Gate (radians)");
	WrapperSlider("Shadow Night Multiplier", iSection);
	WrapperSlider("Shadow Sunset Start", iSection);
	WrapperSlider("Shadow Sunset End", iSection);
	WrapperSlider("Shadow Sunrise Start", iSection);
	WrapperSlider("Shadow Sunrise End", iSection);
}

} // namespace engine

#endif // BT_CLIENT
