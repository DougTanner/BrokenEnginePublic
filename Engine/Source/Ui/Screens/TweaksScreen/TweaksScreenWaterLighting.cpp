#include "TweaksScreen.h"

#include "Game.h"

namespace engine
{

void TweaksScreen::RenderWaterLightingSection()
{
	WrapperSeparatorText("Specular");
	WrapperSlider("Specular Normal Soften", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Normal Blend Wave", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Diffuse", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Direct", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Water Specular", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Intensity", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Add", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular One", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Two", static_cast<int>(TweakSection::kWaterLighting));
	WrapperSlider("Specular Three", static_cast<int>(TweakSection::kWaterLighting));
}

} // namespace engine
