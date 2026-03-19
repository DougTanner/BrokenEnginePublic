#include "TweaksScreen.h"

namespace engine
{

void TweaksScreen::RenderWaterSpecularSection()
{
	WrapperSeparatorText("Normals");
	WrapperSlider("Sampled Normals Size", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Size Mod", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Sampled Normals Speed", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Depth Reflection Feather", static_cast<int>(TweakSection::kWaterSpecular));

	WrapperSeparatorText("Skybox");
	WrapperSlider("Sun Bias", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Normal Soften", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Normal Blend Wave", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Intensity", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Add", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 1", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 1 Power", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 2", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 2 Power", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 3", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox 3 Power", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Skybox Lod", static_cast<int>(TweakSection::kWaterSpecular));

	WrapperSeparatorText("Height Darken");
	WrapperSlider("Height Darken Top", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Bottom", static_cast<int>(TweakSection::kWaterSpecular));
	WrapperSlider("Height Darken Clamp", static_cast<int>(TweakSection::kWaterSpecular));
}

} // namespace engine
