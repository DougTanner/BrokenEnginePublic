#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/ShadowWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gShadowRegistrar
{
	// Quality / Perf
	{"Resolution", &gShadowRenderMultiplier},
	{"Shadow Texel Ramp Speed", &gShadowTexelRampMetersPerSec},
	{"Shadow Temporal Blend", &gShadowTemporalBlend},
	// Feather
	{"Feather Noon", &gShadowFeatherNoon},
	{"Feather Noon Offset", &gShadowFeatherNoonOffset},
	{"Feather Sunset", &gShadowFeatherSunset},
	{"Feather Sunset Offset", &gShadowFeatherSunsetOffset},
	{"Feather Power", &gShadowFeatherPower},
	{"Distance Falloff", &gShadowDistanceFalloff},
	{"Blur Sigma", &gShadowBlurSigma},
	{"Affect Ambient", &gShadowAffectAmbient},
	{"Height Fade Top", &gShadowHeightFadeTop},
	{"Height Fade Bottom", &gShadowHeightFadeBottom},
	// Object Shadows
	{"Render Multiplier", &gObjectShadowsRenderMultiplier},
	{"Blur Multiplier", &gObjectShadowsBlurMultiplier},
	{"Shadow Noon", &gObjectShadowsNoon},
	{"Shadow Sunset", &gObjectShadowsSunset},
	{"Sunset Stretch", &gObjectShadowsSunsetStretch},
	{"Object Blur Sigma", &gObjectShadowsBlurSigma},
	{"Object Blur Radius", &gObjectShadowsBlurRadius},
	{"Object Shadow Grow", &gObjectShadowsGrow},
	{"Smoke Shadow Intensity", &gSmokeShadowIntensity},
};
}

void TweaksScreenBase::RenderShadowSection()
{
	const int64_t iSection = giTweakSectionShadow;

	WrapperSeparatorText("Quality / Perf");
	WrapperSlider("Resolution", iSection);
	WrapperSlider("Texel Contraction Speed", iSection, 2.0f, "Shadow Texel Ramp Speed");
	WrapperSlider("Temporal Blend", iSection, 2.0f, "Shadow Temporal Blend");

	WrapperSeparatorText("Feather");
	WrapperSlider("Feather Noon", iSection);
	WrapperSlider("Feather Noon Offset", iSection);
	WrapperSlider("Feather Sunset", iSection);
	WrapperSlider("Feather Sunset Offset", iSection);
	WrapperSlider("Feather Power", iSection);
	WrapperSlider("Distance Falloff", iSection);
	WrapperSlider("Blur Sigma", iSection);
	WrapperSlider("Affect Ambient", iSection);
	WrapperSlider("Height Fade Top", iSection);
	WrapperSlider("Height Fade Bottom", iSection);

	WrapperSeparatorText("Object Shadows");
	WrapperSlider("Render Multiplier", iSection);
	WrapperSlider("Blur Multiplier", iSection);
	WrapperSlider("Shadow Noon", iSection);
	WrapperSlider("Shadow Sunset", iSection);
	WrapperSlider("Sunset Stretch", iSection);
	WrapperSlider("Object Blur Sigma", iSection);
	WrapperSlider("Object Blur Radius", iSection);
	WrapperSlider("Grow", iSection, 2.0f, "Object Shadow Grow");
	WrapperSlider("Smoke Shadow Intensity", iSection);
}

} // namespace engine

#endif // BT_CLIENT
