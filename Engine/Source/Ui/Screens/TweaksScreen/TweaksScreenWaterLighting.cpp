#include "TweaksScreenBase.h"

namespace engine
{

void TweaksScreenBase::RenderWaterLightingSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWaterLighting);

	WrapperSeparatorText("Specular");
	WrapperSlider("Specular Normal Soften", kiSection);
	WrapperSlider("Specular Normal Blend Wave", kiSection);
	WrapperSlider("Specular Diffuse", kiSection);
	WrapperSlider("Specular Direct", kiSection);
	WrapperSlider("Water Specular", kiSection);
	WrapperSlider("Specular Intensity", kiSection);
	WrapperSlider("Specular Add", kiSection);
	WrapperSlider("Specular One", kiSection);
	WrapperSlider("Specular Two", kiSection);
	WrapperSlider("Specular Three", kiSection);
}

} // namespace engine
