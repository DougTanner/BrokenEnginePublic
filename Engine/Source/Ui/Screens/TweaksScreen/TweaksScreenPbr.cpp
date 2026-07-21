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
	{"Model Data Mip Bias", &gPbrModelDataMipLodBias},
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
	const int64_t iSection = giTweakSectionPbr;

	if (ImGui::BeginTable("PbrColumns", 2))
	{
		// Left column
		ImGui::TableNextColumn();

		WrapperSeparatorText("Engine Variables");
		WrapperSlider("Sun", iSection, 1.0f);
		WrapperSlider("Day Brightness", iSection, 1.0f);
		WrapperSlider("Model Data Mip Bias", iSection, 1.0f);

		WrapperSeparatorText("BRDF");
		WrapperSlider("BRDF Diffuse", iSection, 1.0f);
		WrapperSlider("BRDF Diffuse Power", iSection, 1.0f);
		WrapperSlider("BRDF Specular", iSection, 1.0f);
		WrapperSlider("BRDF Specular Power", iSection, 1.0f);

		WrapperSeparatorText("Tone Mapping");
		WrapperSlider("Exposure", iSection, 1.0f);
		WrapperSlider("Gamma", iSection, 1.0f);

		WrapperSeparatorText("Color Grading");
		WrapperSlider("Saturation", iSection, 1.0f);
		WrapperSlider("Contrast", iSection, 1.0f);
		WrapperSlider("Temperature", iSection, 1.0f);

		WrapperSeparatorText("Post Lighting");
		WrapperSlider("Lighting Specular", iSection, 1.0f);
		WrapperSlider("Lighting Specular Power", iSection, 1.0f);
		WrapperSlider("Lighting", iSection, 1.0f);
		WrapperSlider("Lighting Power", iSection, 1.0f);

		// Right column
		ImGui::TableNextColumn();

		WrapperSeparatorText("IBL");
		WrapperSlider("IBL Ambient", iSection, 1.0f);
		WrapperSlider("IBL Diffuse", iSection, 1.0f);
		WrapperSlider("IBL Diffuse Power", iSection, 1.0f);
		WrapperSlider("IBL Specular", iSection, 1.0f);
		WrapperSlider("IBL Specular Power", iSection, 1.0f);
		WrapperSlider("IBL Shadow Blend", iSection, 1.0f);
		WrapperSlider("IBL Ambient Color Blend", iSection, 1.0f);
		WrapperSlider("Cubemap Lod Power", iSection, 1.0f);
		WrapperSlider("Cubemap Lod Offset", iSection, 1.0f);
		WrapperSlider("Shadow Floor", iSection, 1.0f);

		WrapperSeparatorText("Smoke");
		WrapperSlider("Smoke", iSection, 1.0f);

		WrapperSeparatorText("Emissive");
		WrapperSlider("Emissive", iSection, 1.0f);

		ImGui::EndTable();
	}
}

} // namespace engine

#endif // BT_CLIENT
