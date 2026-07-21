#include "TweaksScreen.h"

#if defined(BT_CLIENT)

namespace game
{

namespace
{
const engine::TweaksSliderMapRegistrar gHexShieldRegistrar
{
	// Edge
	{"Grow", &gHexShieldGrow},
	{"Edge Distance", &gHexShieldEdgeDistance},
	{"Edge Power", &gHexShieldEdgePower},
	{"Edge Multiplier", &gHexShieldEdgeMultiplier},
	// Wave
	{"Wave Multiplier", &gHexShieldWaveMultiplier},
	{"Wave Dot", &gHexShieldWaveDotMultiplier},
	{"Wave Intensity", &gHexShieldWaveIntensityMultiplier},
	{"Wave Intensity Power", &gHexShieldWaveIntensityPower},
	{"Wave Falloff Power", &gHexShieldWaveFalloffPower},
	// Direction
	{"Direction Falloff Power", &gHexShieldDirectionFalloffPower},
	{"Direction Multiplier", &gHexShieldDirectionMultiplier},
};
}

void TweaksScreen::RenderHexShieldSection()
{
	const int64_t iSection = giTweakSectionHexShield;

	WrapperSeparatorText("Edge");
	WrapperSlider("Grow", iSection);
	WrapperSlider("Edge Distance", iSection);
	WrapperSlider("Edge Power", iSection);
	WrapperSlider("Edge Multiplier", iSection);

	WrapperSeparatorText("Wave");
	WrapperSlider("Wave Multiplier", iSection);
	WrapperSlider("Wave Dot", iSection);
	WrapperSlider("Wave Intensity", iSection);
	WrapperSlider("Wave Intensity Power", iSection);
	WrapperSlider("Wave Falloff Power", iSection);

	WrapperSeparatorText("Direction");
	WrapperSlider("Direction Falloff Power", iSection);
	WrapperSlider("Direction Multiplier", iSection);
}

} // namespace game

#endif // BT_CLIENT
