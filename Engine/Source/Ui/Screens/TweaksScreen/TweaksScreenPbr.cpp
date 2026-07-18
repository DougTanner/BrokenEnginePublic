#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/PbrWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gPbrRegistrar
{
	// Engine Variables
	{"Sun", &gPbrSun},
	{"Day Brightness", &gPbrDayBrightness},
	// BRDF
	{"BRDF Diffuse", &gPbrBrdfDiffuse},
	{"BRDF Diffuse Power", &gPbrBrdfDiffusePower},
	{"BRDF Specular", &gPbrBrdfSpecular},
	{"BRDF Specular Power", &gPbrBrdfSpecularPower},
	// Tone Mapping
	{"Exposure", &gPbrExposure},
	{"Gamma", &gPbrGamma},
	// Color Grading
	{"Saturation", &gColorGradingSaturation},
	{"Contrast", &gColorGradingContrast},
	{"Temperature", &gColorGradingTemperature},
	// Post Lighting
	{"Lighting Specular", &gPbrLightingSpecular},
	{"Lighting Specular Power", &gPbrLightingSpecularPower},
	{"Lighting", &gPbrLighting},
	{"Lighting Power", &gPbrLightingPower},
	// IBL
	{"IBL Ambient", &gPbrIblAmbient},
	{"IBL Diffuse", &gPbrIblDiffuse},
	{"IBL Diffuse Power", &gPbrIblDiffusePower},
	{"IBL Specular", &gPbrIblSpecular},
	{"IBL Specular Power", &gPbrIblSpecularPower},
	{"IBL Shadow Blend", &gPbrIblShadowBlend},
	{"IBL Ambient Color Blend", &gPbrIblAmbientColorBlend},
	{"Cubemap Lod Power", &gPbrCubemapLodPower},
	{"Cubemap Lod Offset", &gPbrCubemapLodOffset},
	{"Shadow Floor", &gPbrShadowFloor},
	// Smoke
	{"Smoke", &gPbrSmoke},
	// Emissive
	{"Emissive", &gPbrEmissive},
};
}

void TweaksScreenBase::RenderPbrSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kModel);

	if (ImGui::BeginTable("PbrColumns", 2))
	{
		// Left column
		ImGui::TableNextColumn();

		WrapperSeparatorText("Engine Variables");
		WrapperSlider("Sun", kiSection, 1.0f);
		WrapperSlider("Day Brightness", kiSection, 1.0f);

		WrapperSeparatorText("BRDF");
		WrapperSlider("BRDF Diffuse", kiSection, 1.0f);
		WrapperSlider("BRDF Diffuse Power", kiSection, 1.0f);
		WrapperSlider("BRDF Specular", kiSection, 1.0f);
		WrapperSlider("BRDF Specular Power", kiSection, 1.0f);

		WrapperSeparatorText("Tone Mapping");
		WrapperSlider("Exposure", kiSection, 1.0f);
		WrapperSlider("Gamma", kiSection, 1.0f);

		WrapperSeparatorText("Color Grading");
		WrapperSlider("Saturation", kiSection, 1.0f);
		WrapperSlider("Contrast", kiSection, 1.0f);
		WrapperSlider("Temperature", kiSection, 1.0f);

		WrapperSeparatorText("Post Lighting");
		WrapperSlider("Lighting Specular", kiSection, 1.0f);
		WrapperSlider("Lighting Specular Power", kiSection, 1.0f);
		WrapperSlider("Lighting", kiSection, 1.0f);
		WrapperSlider("Lighting Power", kiSection, 1.0f);

		// Right column
		ImGui::TableNextColumn();

		WrapperSeparatorText("IBL");
		WrapperSlider("IBL Ambient", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse", kiSection, 1.0f);
		WrapperSlider("IBL Diffuse Power", kiSection, 1.0f);
		WrapperSlider("IBL Specular", kiSection, 1.0f);
		WrapperSlider("IBL Specular Power", kiSection, 1.0f);
		WrapperSlider("IBL Shadow Blend", kiSection, 1.0f);
		WrapperSlider("IBL Ambient Color Blend", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Power", kiSection, 1.0f);
		WrapperSlider("Cubemap Lod Offset", kiSection, 1.0f);
		WrapperSlider("Shadow Floor", kiSection, 1.0f);

		WrapperSeparatorText("Smoke");
		WrapperSlider("Smoke", kiSection, 1.0f);

		WrapperSeparatorText("Emissive");
		WrapperSlider("Emissive", kiSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace engine

#endif // BT_CLIENT
