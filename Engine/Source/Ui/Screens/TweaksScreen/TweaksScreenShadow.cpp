#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/ShadowWrappersBase.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gShadowRegistrar
{
	// Quality / Perf
	{"Resolution", &gShadowRenderMultiplier},
	{"Texel Ramp Speed", &gShadowTexelRampMetersPerSec},
	{"Temporal Blend", &gShadowTemporalBlend},
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
	{"Grow", &gObjectShadowsGrow},
	{"Smoke Shadow Intensity", &gSmokeShadowIntensity},
};
}

void TweaksScreenBase::RenderShadowSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kShadow);

	WrapperSeparatorText("Quality / Perf");
	WrapperSlider("Resolution", kiSection);
	WrapperSlider("Texel Ramp Speed", kiSection);
	WrapperSlider("Temporal Blend", kiSection);

	WrapperSeparatorText("Feather");
	WrapperSlider("Feather Noon", kiSection);
	WrapperSlider("Feather Noon Offset", kiSection);
	WrapperSlider("Feather Sunset", kiSection);
	WrapperSlider("Feather Sunset Offset", kiSection);
	WrapperSlider("Feather Power", kiSection);
	WrapperSlider("Distance Falloff", kiSection);
	WrapperSlider("Blur Sigma", kiSection);
	WrapperSlider("Affect Ambient", kiSection);
	WrapperSlider("Height Fade Top", kiSection);
	WrapperSlider("Height Fade Bottom", kiSection);

	WrapperSeparatorText("Object Shadows");
	WrapperSlider("Render Multiplier", kiSection);
	WrapperSlider("Blur Multiplier", kiSection);
	WrapperSlider("Shadow Noon", kiSection);
	WrapperSlider("Shadow Sunset", kiSection);
	WrapperSlider("Sunset Stretch", kiSection);
	WrapperSlider("Object Blur Sigma", kiSection);
	WrapperSlider("Object Blur Radius", kiSection);
	WrapperSlider("Grow", kiSection);
	WrapperSlider("Smoke Shadow Intensity", kiSection);
}

} // namespace engine

#endif // BT_CLIENT
