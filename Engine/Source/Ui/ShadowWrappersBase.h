#pragma once

#include "WrapperBase.h"

namespace engine
{

// Quality / Perf
extern Wrapper gShadowRenderMultiplier;
extern Wrapper gShadowTexelRampMetersPerSec;
extern Wrapper gShadowTemporalBlend;

// Feather
extern Wrapper gShadowFeatherNoon;
extern Wrapper gShadowFeatherNoonOffset;
extern Wrapper gShadowFeatherSunset;
extern Wrapper gShadowFeatherSunsetOffset;
extern Wrapper gShadowFeatherPower;
extern Wrapper gShadowDistanceFalloff;
extern Wrapper gShadowBlurSigma;
extern Wrapper gShadowAffectAmbient;
extern Wrapper gShadowHeightFadeTop;
extern Wrapper gShadowHeightFadeBottom;

// Object Shadows
extern Wrapper gObjectShadowsRenderMultiplier;
extern Wrapper gObjectShadowsBlurMultiplier;
extern Wrapper gObjectShadowsNoon;
extern Wrapper gObjectShadowsSunset;
extern Wrapper gObjectShadowsSunsetStretch;
extern Wrapper gObjectShadowsBlurSigma;
extern Wrapper gObjectShadowsBlurRadius;
extern Wrapper gObjectShadowsGrow;
extern Wrapper gSmokeShadowIntensity;

} // namespace engine
