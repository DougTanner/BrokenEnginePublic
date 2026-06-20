#include "TweaksScreenBase.h"

#include "TweaksSliderMap.h"
#include "Ui/WaterWrappersBase.h"
#include "Graphics/Managers/TextureManager.h"

#if defined(BT_CLIENT)

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gWaterRegistrar
{
	// Specular - Normals (per-sample row layout: chevron | size | weight-min | weight-max | rotation)
	{"Size 1", &gLightingSampledNormalsOneSize},
	{"Weight Min 1", &gLightingSampledNormalsWeightOneMin},
	{"Weight Max 1", &gLightingSampledNormalsWeightOneMax},
	{"Rotation 1", &gWaterNormalRotationOne},
	{"Size 2", &gLightingSampledNormalsTwoSize},
	{"Weight Min 2", &gLightingSampledNormalsWeightTwoMin},
	{"Weight Max 2", &gLightingSampledNormalsWeightTwoMax},
	{"Rotation 2", &gWaterNormalRotationTwo},
	{"Size 3", &gLightingSampledNormalsThreeSize},
	{"Weight Min 3", &gLightingSampledNormalsWeightThreeMin},
	{"Weight Max 3", &gLightingSampledNormalsWeightThreeMax},
	{"Rotation 3", &gWaterNormalRotationThree},
	{"Speed Min", &gLightingSampledNormalsSpeedMin},
	{"Speed Max", &gLightingSampledNormalsSpeedMax},
	{"Depth Reflection Feather", &gWaterDepthReflectionFeather},
	{"Wave Normal Blend (Global)", &gWaterWaveNormalBlend},
	// Specular - Skybox
	{"Sun Bias", &gLightingWaterSkyboxSunBias},
	{"Normal Soften", &gLightingWaterSkyboxNormalSoften},
	{"Normal Blend Wave", &gLightingWaterSkyboxNormalBlendWave},
	{"Intensity", &gLightingWaterSkyboxIntensity},
	{"Add", &gLightingWaterSkyboxAdd},
	{"Skybox 1", &gLightingWaterSkyboxOne},
	{"Skybox 1 Power", &gLightingWaterSkyboxOnePower},
	{"Skybox 2", &gLightingWaterSkyboxTwo},
	{"Skybox 2 Power", &gLightingWaterSkyboxTwoPower},
	{"Skybox 3", &gLightingWaterSkyboxThree},
	{"Skybox 3 Power", &gLightingWaterSkyboxThreePower},
	{"Skybox Lod", &gLightingWaterSkyboxLod},
	{"Skybox One Resolution", &gWaterSkyboxOneRenderMultiplier},
	// Specular - Height Darken
	{"Height Darken Top", &gWaterHeightDarkenTop},
	{"Height Darken Bottom", &gWaterHeightDarkenBottom},
	{"Height Darken Target", &gWaterHeightDarkenTarget},
	{"Height Darken Source", &gWaterHeightDarkenSource},
	{"Height Darken Lighting", &gWaterHeightDarkenLighting},
	// Low - Wave
	{"Low Max", &gWaterLowMax},
	{"Angle", &gWaterLowAngle},
	{"Wavelength", &gWaterLowWavelength},
	{"Amplitude", &gWaterLowAmplitude},
	{"Speed", &gWaterLowSpeed},
	{"Steepness", &gWaterLowSteepness},
	// Low - Adjustments
	{"Angle Adjust", &gWaterLowAngleAdjust},
	{"Wavelength Adjust", &gWaterLowWavelengthAdjust},
	{"Amplitude Adjust", &gWaterLowAmplitudeAdjust},
	{"Speed Adjust", &gWaterLowSpeedAdjust},
	// Low - Beach Fade
	{"Beach Fade Top", &gWaterBeachFadeTop},
	{"Beach Fade Bottom", &gWaterBeachFadeBottom},
	// Low - Camera Fade
	{"Low Camera Fade Start", &gWaterLowAmplitudeFadeStart},
	{"Low Camera Fade End", &gWaterLowAmplitudeFadeEnd},
	// Medium - Wave
	{"Medium Wavelength", &gWaterMediumWavelength},
	{"Medium Amplitude", &gWaterMediumAmplitude},
	{"Medium Speed", &gWaterMediumSpeed},
	{"Medium Steepness", &gWaterMediumSteepness},
	// Medium - Adjustments
	{"Medium Angle Adjust", &gWaterMediumAngleAdjust},
	{"Medium Wavelength Adjust", &gWaterMediumWavelengthAdjust},
	{"Medium Amplitude Adjust", &gWaterMediumAmplitudeAdjust},
	{"Medium Speed Adjust", &gWaterMediumSpeedAdjust},
	// Medium - Camera Fade
	{"Medium Camera Fade Start", &gWaterMediumAmplitudeFadeStart},
	{"Medium Camera Fade End", &gWaterMediumAmplitudeFadeEnd},
	// Depth
	{"Water Terrain Height", &gWaterTerrainHeight},
	{"DT: TEMP Z Offset", &gWaterZOffsetTemp}, // DT: TEMP
	{"Water Terrain Fade", &gWaterTerrainFade},
	{"Water Terrain Fade Clamp", &gWaterTerrainFadeClamp},
	{"Water Height", &gWaterHeight},
	{"Water Early Out", &gWaterEarlyOut},
	{"Water Depth Lut Feather", &gWaterDepthLutFeather},
	{"Water Depth Lut Sunset Fade Power", &gWaterDepthLutSunsetFadePower},
	{"Water Depth Lut Sunset Fade Intensity", &gWaterDepthLutSunsetFadeIntensity},
	{"Water Depth Color Feather", &gWaterDepthColorFeather},
	{"Water Depth Color Floor", &gWaterDepthColorFloor},
	{"Undersea Compression", &gWaterUnderseaCompression},
	{"Water Color Bottom", &gWaterColorBottom},
	{"Water Color Height", &gWaterColorHeight},
	{"Water Fresnel", &gWaterFresnel},
	{"Water Noise Frequency", &gWaterNoiseFrequency},
	{"Water Noise Amount", &gWaterNoiseAmount},
	{"Water Color Noise Frequency", &gWaterColorNoiseFrequency},
	{"Water Color Noise Amount", &gWaterColorNoiseAmount},
	{"Water Color Noise Weight One", &gWaterColorNoiseWeightOne},
	{"Water Color Noise Multiplier One", &gWaterColorNoiseMultiplierOne},
	{"Water Color Noise Weight Two", &gWaterColorNoiseWeightTwo},
	{"Water Color Noise Multiplier Two", &gWaterColorNoiseMultiplierTwo},
};
}

void TweaksScreenBase::RenderWaterSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kWater);

	if (ImGui::BeginTabBar("WaterTabs"))
	{
		if (ImGui::BeginTabItem("Specular", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 0;
			}
			if (ImGui::BeginTable("WaterSpecularColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Normals");
				ChevronIndexSelector("Sample 1", gWaterNormalIndexOne, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 1", kiSection, 1.0f);
				WrapperSlider("Weight Min 1", kiSection, 1.0f);
				WrapperSlider("Weight Max 1", kiSection, 1.0f);
				WrapperSlider("Rotation 1", kiSection, 1.0f);

				ChevronIndexSelector("Sample 2", gWaterNormalIndexTwo, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 2", kiSection, 1.0f);
				WrapperSlider("Weight Min 2", kiSection, 1.0f);
				WrapperSlider("Weight Max 2", kiSection, 1.0f);
				WrapperSlider("Rotation 2", kiSection, 1.0f);

				ChevronIndexSelector("Sample 3", gWaterNormalIndexThree, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 3", kiSection, 1.0f);
				WrapperSlider("Weight Min 3", kiSection, 1.0f);
				WrapperSlider("Weight Max 3", kiSection, 1.0f);
				WrapperSlider("Rotation 3", kiSection, 1.0f);

				WrapperSlider("Speed Min", kiSection, 1.0f);
				WrapperSlider("Speed Max", kiSection, 1.0f);
				WrapperSlider("Depth Reflection Feather", kiSection, 1.0f);
				WrapperSlider("Wave Normal Blend (Global)", kiSection, 1.0f);

				WrapperSeparatorText("Skybox");
				WrapperSlider("Sun Bias", kiSection, 1.0f);
				WrapperSlider("Normal Soften", kiSection, 1.0f);
				WrapperSlider("Normal Blend Wave", kiSection, 1.0f);
				WrapperSlider("Intensity", kiSection, 1.0f);
				WrapperSlider("Add", kiSection, 1.0f);

				ImGui::TableNextColumn();

				WrapperSeparatorText("Skybox");
				WrapperSlider("Skybox 1", kiSection, 1.0f);
				WrapperSlider("Skybox 1 Power", kiSection, 1.0f);
				WrapperSlider("Skybox 2", kiSection, 1.0f);
				WrapperSlider("Skybox 2 Power", kiSection, 1.0f);
				WrapperSlider("Skybox 3", kiSection, 1.0f);
				WrapperSlider("Skybox 3 Power", kiSection, 1.0f);
				WrapperSlider("Skybox Lod", kiSection, 1.0f);
				WrapperSlider("Skybox One Resolution", kiSection, 1.0f);

				WrapperSeparatorText("Height Darken");
				WrapperSlider("Height Darken Top", kiSection, 1.0f);
				WrapperSlider("Height Darken Bottom", kiSection, 1.0f);
				WrapperSlider("Height Darken Target", kiSection, 1.0f);
				WrapperSlider("Height Darken Source", kiSection, 1.0f);
				WrapperSlider("Height Darken Lighting", kiSection, 1.0f);

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Low", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 1;
			}
			RenderWaveCountRadioButtons(gWaterLowCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Low Max", kiSection);
			WrapperSlider("Angle", kiSection);
			WrapperSlider("Wavelength", kiSection);
			WrapperSlider("Amplitude", kiSection);
			WrapperSlider("Speed", kiSection);
			WrapperSlider("Steepness", kiSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Angle Adjust", kiSection);
			WrapperSlider("Wavelength Adjust", kiSection);
			WrapperSlider("Amplitude Adjust", kiSection);
			WrapperSlider("Speed Adjust", kiSection);

			WrapperSeparatorText("Beach Fade");
			WrapperSlider("Beach Fade Top", kiSection);
			WrapperSlider("Beach Fade Bottom", kiSection);

			WrapperSeparatorText("Camera Fade");
			WrapperSlider("Low Camera Fade Start", kiSection);
			WrapperSlider("Low Camera Fade End", kiSection);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Medium", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 2)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 2;
			}
			RenderWaveCountRadioButtons(gWaterMediumCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Medium Wavelength", kiSection);
			WrapperSlider("Medium Amplitude", kiSection);
			WrapperSlider("Medium Speed", kiSection);
			WrapperSlider("Medium Steepness", kiSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Medium Angle Adjust", kiSection);
			WrapperSlider("Medium Wavelength Adjust", kiSection);
			WrapperSlider("Medium Amplitude Adjust", kiSection);
			WrapperSlider("Medium Speed Adjust", kiSection);

			WrapperSeparatorText("Camera Fade");
			WrapperSlider("Medium Camera Fade Start", kiSection);
			WrapperSlider("Medium Camera Fade End", kiSection);

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Depth", nullptr, ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 3) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if ((mApplySubtab & SectionFlag(kiSection)) && mActiveSubtab[kiSection] == 3)
			{
				mApplySubtab.Clear(SectionFlag(kiSection));
			}
			if (!(mApplySubtab & SectionFlag(kiSection)))
			{
				mActiveSubtab[kiSection] = 3;
			}
			WrapperSeparatorText("Terrain Fade");
			WrapperSlider("Water Terrain Height", kiSection);
			WrapperSlider("DT: TEMP Z Offset", kiSection); // DT: TEMP
			WrapperSlider("Water Terrain Fade", kiSection);
			WrapperSlider("Water Terrain Fade Clamp", kiSection);

			WrapperSeparatorText("Surface");
			WrapperSlider("Water Height", kiSection);
			WrapperSlider("Water Early Out", kiSection);

			WrapperSeparatorText("Depth Color");
			WrapperSlider("Water Depth Lut Feather", kiSection);
			WrapperSlider("Water Depth Lut Sunset Fade Power", kiSection);
			WrapperSlider("Water Depth Lut Sunset Fade Intensity", kiSection);
			WrapperSlider("Water Depth Color Feather", kiSection);
			WrapperSlider("Water Depth Color Floor", kiSection);
			WrapperSlider("Undersea Compression", kiSection);
			WrapperSlider("Water Color Bottom", kiSection);
			WrapperSlider("Water Color Height", kiSection);

			WrapperSeparatorText("Fresnel");
			WrapperSlider("Water Fresnel", kiSection);

			WrapperSeparatorText("Normal Noise");
			WrapperSlider("Water Noise Frequency", kiSection);
			WrapperSlider("Water Noise Amount", kiSection);

			WrapperSeparatorText("Color Noise");
			WrapperSlider("Water Color Noise Frequency", kiSection);
			WrapperSlider("Water Color Noise Amount", kiSection);
			WrapperSlider("Water Color Noise Weight One", kiSection);
			WrapperSlider("Water Color Noise Multiplier One", kiSection);
			WrapperSlider("Water Color Noise Weight Two", kiSection);
			WrapperSlider("Water Color Noise Multiplier Two", kiSection);

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
