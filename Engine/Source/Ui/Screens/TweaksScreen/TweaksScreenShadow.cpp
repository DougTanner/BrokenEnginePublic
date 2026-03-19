#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderShadowSection()
{
	WrapperSeparatorText("Feather");
	WrapperSlider("Feather Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Noon Offset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Sunset Offset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Feather Power", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Distance Falloff", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Sigma", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Affect Ambient", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Height Fade Top", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Height Fade Bottom", static_cast<int>(TweakSection::kShadow));

	WrapperSeparatorText("Object Shadows");
	WrapperSlider("Render Multiplier", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Multiplier", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Shadow Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Shadow Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Sunset Stretch", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Distance Noon", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Blur Distance Sunset", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Object Blur Sigma", static_cast<int>(TweakSection::kShadow));
	WrapperSlider("Smoke Shadow Intensity", static_cast<int>(TweakSection::kShadow));
}

} // namespace engine
