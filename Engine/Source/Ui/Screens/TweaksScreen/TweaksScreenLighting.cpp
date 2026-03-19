#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderLightingSection()
{
	WrapperSeparatorText("Blur");
	WrapperSlider("Texture Multiplier", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Distance", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Directionality", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Jitter", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Downscale", static_cast<int>(TweakSection::kLighting));

	WrapperSeparatorText("Combine");
	WrapperSlider("Combine Index", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur First Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Blur Divisor", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Decay", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Combine Power", static_cast<int>(TweakSection::kLighting));

	WrapperSeparatorText("Directional");
	WrapperSlider("Directional", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Indirect", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Terrain", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Terrain Add", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Objects", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Objects Add", static_cast<int>(TweakSection::kLighting));
	WrapperSlider("Time of Day Multiplier", static_cast<int>(TweakSection::kLighting));
}

} // namespace engine
