#include "SunMoonWrappersBase.h"

namespace engine
{

// Color Phase Boundaries (radians)
Wrapper gSunMoonMorning(XM_PIDIV16, 0.0f, XM_PIDIV4);
Wrapper gSunMoonNoonStart(XM_PIDIV8, 0.0f, XM_PIDIV2);
Wrapper gSunMoonNoonEnd(XM_PIDIV2 + XM_PIDIV8, XM_PIDIV2, XM_PI - XM_PIDIV16);
Wrapper gSunMoonEvening(XM_PI - XM_PIDIV16, XM_PIDIV2, XM_PI);
Wrapper gSunMoonNightStart(XM_PI, XM_PIDIV2, XM_PI + XM_PIDIV2);

// Sun Intensity (per target)
Wrapper gSunMoonSunIntensityTerrain(1.1f, 0.0f, 4.0f);
Wrapper gSunMoonSunIntensityWater(0.4f, 0.0f, 4.0f);
Wrapper gSunMoonSunIntensityObjects(1.0f, 0.0f, 4.0f);
Wrapper gSunMoonSunIntensitySmoke(1.0f, 0.0f, 4.0f);

// Moon Intensity (per target) + Blue Tint
Wrapper gSunMoonMoonIntensityTerrain(0.08f, 0.0f, 0.1f);
Wrapper gSunMoonMoonIntensityWater(0.015f, 0.0f, 0.1f);
Wrapper gSunMoonMoonIntensityObjects(0.05f, 0.0f, 1.0f);
Wrapper gSunMoonMoonIntensitySmoke(0.05f, 0.0f, 1.0f);
Wrapper gSunMoonMoonBlueTint(1.8f, 1.0f, 4.0f);

// Moon Timing (radians)
Wrapper gSunMoonMoonriseStart(3.05f, XM_PIDIV2, XM_2PI);
Wrapper gSunMoonMoonriseEnd(3.1f, XM_PIDIV2, XM_2PI);
Wrapper gSunMoonMoonsetStart(XM_PIDIV128, 0.0f, XM_PIDIV2);
Wrapper gSunMoonMoonsetEnd(XM_PIDIV32, 0.0f, XM_PIDIV2);

// Ambient
Wrapper gSunMoonMinimumAmbient(0.02f, 0.0f, 0.1f);
Wrapper gSunMoonAmbientMultiplier(2.5f, 0.0f, 4.0f);

// Normal Tilt (radians, applied to f4SunMoonNormal only — does not affect shadows)
Wrapper gSunMoonNormalTilt(-0.45f, -XM_PIDIV2, XM_PIDIV2);

// Shadow Night-Gate (radians)
Wrapper gSunMoonShadowNightMultiplier(0.2f, 0.0f, 1.0f);
Wrapper gSunMoonShadowSunsetStart(XM_PI - XM_PIDIV32, XM_PIDIV2, XM_PI);
Wrapper gSunMoonShadowSunsetEnd(XM_PI - XM_PIDIV128, XM_PIDIV2, XM_PI);
Wrapper gSunMoonShadowSunriseStart(XM_PIDIV128, 0.0f, XM_PIDIV2);
Wrapper gSunMoonShadowSunriseEnd(XM_PIDIV32, 0.0f, XM_PIDIV2);

} // namespace engine
