#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderHexShieldSection()
{
	WrapperSeparatorText("Edge");
	WrapperSlider("Grow", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Distance", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Edge Multiplier", static_cast<int>(TweakSection::kHexShield));

	WrapperSeparatorText("Wave");
	WrapperSlider("Wave Multiplier", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Dot", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Intensity Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Wave Falloff Power", static_cast<int>(TweakSection::kHexShield));

	WrapperSeparatorText("Direction");
	WrapperSlider("Direction Falloff Power", static_cast<int>(TweakSection::kHexShield));
	WrapperSlider("Direction Multiplier", static_cast<int>(TweakSection::kHexShield));
}

} // namespace engine
