#pragma once

#include "WrapperBase.h"

namespace engine
{

// Color Phase Boundaries (radians)
extern Wrapper gSunMoonMorning;
extern Wrapper gSunMoonNoonStart;
extern Wrapper gSunMoonNoonEnd;
extern Wrapper gSunMoonEvening;
extern Wrapper gSunMoonNightStart;

// Sun Intensity (per target)
extern Wrapper gSunMoonSunIntensityTerrain;
extern Wrapper gSunMoonSunIntensityWater;
extern Wrapper gSunMoonSunIntensityObjects;
extern Wrapper gSunMoonSunIntensitySmoke;

// Moon Intensity (per target) + Blue Tint
extern Wrapper gSunMoonMoonIntensityTerrain;
extern Wrapper gSunMoonMoonIntensityWater;
extern Wrapper gSunMoonMoonIntensityObjects;
extern Wrapper gSunMoonMoonIntensitySmoke;
extern Wrapper gSunMoonMoonBlueTint;

// Moon Timing (radians)
extern Wrapper gSunMoonMoonriseStart;
extern Wrapper gSunMoonMoonriseEnd;
extern Wrapper gSunMoonMoonsetStart;
extern Wrapper gSunMoonMoonsetEnd;

// Ambient
extern Wrapper gSunMoonMinimumAmbient;
extern Wrapper gSunMoonAmbientMultiplier;

// Normal Tilt (radians, applied to f4SunMoonNormal only — does not affect shadows)
extern Wrapper gSunMoonNormalTilt;

// Shadow Night-Gate (radians)
extern Wrapper gSunMoonShadowNightMultiplier;
extern Wrapper gSunMoonShadowSunsetStart;
extern Wrapper gSunMoonShadowSunsetEnd;
extern Wrapper gSunMoonShadowSunriseStart;
extern Wrapper gSunMoonShadowSunriseEnd;

} // namespace engine
