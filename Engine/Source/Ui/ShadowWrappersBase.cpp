#include "ShadowWrappersBase.h"

namespace engine
{

// Quality
Wrapper gShadowRenderMultiplier(0.1f, 0.1f, 1.0f);
Wrapper gShadowTexelRampMetersPerSec(150.0f, 10.0f, 2000.0f);
Wrapper gShadowTemporalBlend(0.2f, 0.05f, 1.0f);

// Feather
Wrapper gShadowFeatherNoon(2.5f, 0.0f, 8.0f);
Wrapper gShadowFeatherNoonOffset(2.1f, 2.1f, 5.0f);
Wrapper gShadowFeatherSunset(0.1f, 0.0f, 0.5f);
Wrapper gShadowFeatherSunsetOffset(-0.1f, -0.5f, 0.1f);
Wrapper gShadowFeatherPower(1.3f, 0.1f, 10.0f);
Wrapper gShadowDistanceFalloff(200.0f, 10.0f, 400.0f);
Wrapper gShadowBlurSigma(1.0f, 0.1f, 5.0f);
Wrapper gShadowAffectAmbient(0.8f, 0.0f, 1.0f);
Wrapper gShadowHeightFadeTop(5.0f, 0.0f, 20.0f);
Wrapper gShadowHeightFadeBottom(-2.0f, -30.0f, 0.0f);

// Object Shadows
Wrapper gObjectShadowsRenderMultiplier(0.5f, 0.25f, 4.0f);
Wrapper gObjectShadowsBlurMultiplier(0.5f, 0.125f, 1.0f);
Wrapper gObjectShadowsNoon(0.6f, 0.1f, 1.0f);
Wrapper gObjectShadowsSunset(0.4f, 0.01f, 1.0f);
Wrapper gObjectShadowsSunsetStretch(2.5f, 0.0f, 10.0f);
Wrapper gObjectShadowsBlurSigma(8.0f, 1.0f, 40.0f);
Wrapper gObjectShadowsBlurRadius(8.0f, 1.0f, 32.0f, 1.0f);
Wrapper gObjectShadowsGrow(0.2f, 0.0f, 2.0f);
Wrapper gSmokeShadowIntensity(0.5f, 0.0f, 1.0f);

} // namespace engine
