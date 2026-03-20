#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderWaterSpecularSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterSpecular);

	WrapperSeparatorText("Normals");
	WrapperSlider("Sampled Normals Size", kiSection);
	WrapperSlider("Sampled Normals Size Mod", kiSection);
	WrapperSlider("Sampled Normals Speed", kiSection);
	WrapperSlider("Depth Reflection Feather", kiSection);

	WrapperSeparatorText("Skybox");
	WrapperSlider("Sun Bias", kiSection);
	WrapperSlider("Normal Soften", kiSection);
	WrapperSlider("Normal Blend Wave", kiSection);
	WrapperSlider("Intensity", kiSection);
	WrapperSlider("Add", kiSection);
	WrapperSlider("Skybox 1", kiSection);
	WrapperSlider("Skybox 1 Power", kiSection);
	WrapperSlider("Skybox 2", kiSection);
	WrapperSlider("Skybox 2 Power", kiSection);
	WrapperSlider("Skybox 3", kiSection);
	WrapperSlider("Skybox 3 Power", kiSection);
	WrapperSlider("Skybox Lod", kiSection);

	WrapperSeparatorText("Height Darken");
	WrapperSlider("Height Darken Top", kiSection);
	WrapperSlider("Height Darken Bottom", kiSection);
	WrapperSlider("Height Darken Clamp", kiSection);
}

} // namespace engine
