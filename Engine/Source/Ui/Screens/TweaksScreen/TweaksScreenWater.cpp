#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/WaterWrappersBase.h"
#include "Graphics/Managers/TextureManager.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gWaterRegistrar
{
	// Specular - Normals (per-sample row layout: chevron | size | weight-min | weight-max | rotation | speed-min | speed-max | speed-direction)
	{"Size 1", &gLightingSampledNormalsOneSize},
	{"Weight Min 1", &gLightingSampledNormalsWeightOneMin},
	{"Weight Max 1", &gLightingSampledNormalsWeightOneMax},
	{"Rotation 1", &gWaterNormalRotationOne},
	{"Speed Min 1", &gLightingSampledNormalsSpeedOneMin},
	{"Speed Max 1", &gLightingSampledNormalsSpeedOneMax},
	{"Speed Direction 1", &gWaterNormalSpeedDirectionOne},
	{"Size 2", &gLightingSampledNormalsTwoSize},
	{"Weight Min 2", &gLightingSampledNormalsWeightTwoMin},
	{"Weight Max 2", &gLightingSampledNormalsWeightTwoMax},
	{"Rotation 2", &gWaterNormalRotationTwo},
	{"Speed Min 2", &gLightingSampledNormalsSpeedTwoMin},
	{"Speed Max 2", &gLightingSampledNormalsSpeedTwoMax},
	{"Speed Direction 2", &gWaterNormalSpeedDirectionTwo},
	{"Size 3", &gLightingSampledNormalsThreeSize},
	{"Weight Min 3", &gLightingSampledNormalsWeightThreeMin},
	{"Weight Max 3", &gLightingSampledNormalsWeightThreeMax},
	{"Rotation 3", &gWaterNormalRotationThree},
	{"Speed Min 3", &gLightingSampledNormalsSpeedThreeMin},
	{"Speed Max 3", &gLightingSampledNormalsSpeedThreeMax},
	{"Speed Direction 3", &gWaterNormalSpeedDirectionThree},
	{"Depth Reflection Feather", &gWaterDepthReflectionFeather},
	{"Wave Normal Blend (Global)", &gWaterWaveNormalBlend},
	// Specular - Skybox
	{"Sun Bias", &gLightingWaterSkyboxSunBias},
	{"Normal Soften Sunrise", &gLightingWaterSkyboxNormalSoftenSunrise},
	{"Normal Soften Noon", &gLightingWaterSkyboxNormalSoftenNoon},
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
	{"Spec AA Variance", &gWaterSpecAAVariance},
	{"Spec AA Threshold", &gWaterSpecAAThreshold},
	{"Spec AA Mip Scale", &gWaterSpecAAMipScale},
	{"Normal Mip Bias", &gWaterNormalMipBias},
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
	{"Water Color Noise Frequency", &gWaterColorNoiseFrequency},
	{"Water Color Noise Amount", &gWaterColorNoiseAmount},
	{"Water Color Noise Weight One", &gWaterColorNoiseWeightOne},
	{"Water Color Noise Multiplier One", &gWaterColorNoiseMultiplierOne},
	{"Water Color Noise Weight Two", &gWaterColorNoiseWeightTwo},
	{"Water Color Noise Multiplier Two", &gWaterColorNoiseMultiplierTwo},
	// Beach
	{"Break Start Depth", &gWaterBreakStartDepth},
	{"Break End Depth", &gWaterBreakEndDepth},
	{"Break Blend Curve", &gWaterBreakBlendCurve},
	{"Medium Shore Softness", &gWaterMediumShoreSoftness},
	{"Beach Skybox 1 Reduction", &gLightingWaterSkyboxOneBeachReduction},
	{"Beach Skybox 2 Reduction", &gLightingWaterSkyboxTwoBeachReduction},
	{"Beach Skybox 3 Reduction", &gLightingWaterSkyboxThreeBeachReduction},
};
}

void TweaksScreenBase::RenderWaterSection()
{
	const int64_t iSection = giTweakSectionWater;

	if (ImGui::BeginTabBar("WaterTabs"))
	{
		if (BeginSubtab("Specular", iSection, 0))
		{
			if (ImGui::BeginTable("WaterSpecularColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Normals");
				ChevronIndexSelector("Sample 1", gWaterNormalIndexOne, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 1", iSection, 1.0f);
				WrapperSlider("Weight Min 1", iSection, 1.0f);
				WrapperSlider("Weight Max 1", iSection, 1.0f);
				WrapperSlider("Rotation 1", iSection, 1.0f);
				WrapperSlider("Speed Min 1", iSection, 1.0f);
				WrapperSlider("Speed Max 1", iSection, 1.0f);
				WrapperSlider("Speed Direction 1", iSection, 1.0f);

				ChevronIndexSelector("Sample 2", gWaterNormalIndexTwo, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 2", iSection, 1.0f);
				WrapperSlider("Weight Min 2", iSection, 1.0f);
				WrapperSlider("Weight Max 2", iSection, 1.0f);
				WrapperSlider("Rotation 2", iSection, 1.0f);
				WrapperSlider("Speed Min 2", iSection, 1.0f);
				WrapperSlider("Speed Max 2", iSection, 1.0f);
				WrapperSlider("Speed Direction 2", iSection, 1.0f);

				ChevronIndexSelector("Sample 3", gWaterNormalIndexThree, TextureManager::kpWaterNormalNames, TextureManager::kiWaterNormalCount);
				WrapperSlider("Size 3", iSection, 1.0f);
				WrapperSlider("Weight Min 3", iSection, 1.0f);
				WrapperSlider("Weight Max 3", iSection, 1.0f);
				WrapperSlider("Rotation 3", iSection, 1.0f);
				WrapperSlider("Speed Min 3", iSection, 1.0f);
				WrapperSlider("Speed Max 3", iSection, 1.0f);
				WrapperSlider("Speed Direction 3", iSection, 1.0f);

				WrapperSlider("Depth Reflection Feather", iSection, 1.0f);
				WrapperSlider("Wave Normal Blend (Global)", iSection, 1.0f);

				ImGui::TableNextColumn();

				WrapperSeparatorText("Skybox");
				WrapperSlider("Sun Bias", iSection, 1.0f);
				WrapperSlider("Normal Soften Sunrise", iSection, 1.0f);
				WrapperSlider("Normal Soften Noon", iSection, 1.0f);
				WrapperSlider("Normal Blend Wave", iSection, 1.0f);
				WrapperSlider("Intensity", iSection, 1.0f);
				WrapperSlider("Add", iSection, 1.0f);
				WrapperSlider("Skybox 1", iSection, 1.0f);
				WrapperSlider("Skybox 1 Power", iSection, 1.0f);
				WrapperSlider("Skybox 2", iSection, 1.0f);
				WrapperSlider("Skybox 2 Power", iSection, 1.0f);
				WrapperSlider("Skybox 3", iSection, 1.0f);
				WrapperSlider("Skybox 3 Power", iSection, 1.0f);
				WrapperSlider("Skybox Lod", iSection, 1.0f);
				WrapperSlider("Spec AA Variance", iSection, 1.0f);
				WrapperSlider("Spec AA Threshold", iSection, 1.0f);
				WrapperSlider("Spec AA Mip Scale", iSection, 1.0f);
				WrapperSlider("Normal Mip Bias", iSection, 1.0f);

				WrapperSeparatorText("Height Darken");
				WrapperSlider("Height Darken Top", iSection, 1.0f);
				WrapperSlider("Height Darken Bottom", iSection, 1.0f);
				WrapperSlider("Height Darken Target", iSection, 1.0f);
				WrapperSlider("Height Darken Source", iSection, 1.0f);
				WrapperSlider("Height Darken Lighting", iSection, 1.0f);

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (BeginSubtab("Low", iSection, 1))
		{
			RenderWaveCountRadioButtons(gWaterLowCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Low Max", iSection);
			WrapperSlider("Angle", iSection);
			WrapperSlider("Wavelength", iSection);
			WrapperSlider("Amplitude", iSection);
			WrapperSlider("Speed", iSection);
			WrapperSlider("Steepness", iSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Angle Adjust", iSection);
			WrapperSlider("Wavelength Adjust", iSection);
			WrapperSlider("Amplitude Adjust", iSection);
			WrapperSlider("Speed Adjust", iSection);

			WrapperSeparatorText("Camera Fade");
			WrapperSlider("Low Camera Fade Start", iSection);
			WrapperSlider("Low Camera Fade End", iSection);

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Medium", iSection, 2))
		{
			RenderWaveCountRadioButtons(gWaterMediumCount);

			WrapperSeparatorText("Wave");
			WrapperSlider("Medium Wavelength", iSection);
			WrapperSlider("Medium Amplitude", iSection);
			WrapperSlider("Medium Speed", iSection);
			WrapperSlider("Medium Steepness", iSection);

			WrapperSeparatorText("Adjustments");
			WrapperSlider("Medium Angle Adjust", iSection);
			WrapperSlider("Medium Wavelength Adjust", iSection);
			WrapperSlider("Medium Amplitude Adjust", iSection);
			WrapperSlider("Medium Speed Adjust", iSection);

			WrapperSeparatorText("Camera Fade");
			WrapperSlider("Medium Camera Fade Start", iSection);
			WrapperSlider("Medium Camera Fade End", iSection);

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Depth", iSection, 3))
		{
			WrapperSeparatorText("Terrain Fade");
			WrapperSlider("Water Terrain Height", iSection);
			WrapperSlider("DT: TEMP Z Offset", iSection); // DT: TEMP
			WrapperSlider("Water Terrain Fade", iSection);
			WrapperSlider("Water Terrain Fade Clamp", iSection);

			WrapperSeparatorText("Surface");
			WrapperSlider("Water Height", iSection);
			WrapperSlider("Water Early Out", iSection);

			WrapperSeparatorText("Depth Color");
			WrapperSlider("Water Depth Lut Feather", iSection);
			WrapperSlider("Water Depth Lut Sunset Fade Power", iSection);
			WrapperSlider("Water Depth Lut Sunset Fade Intensity", iSection);
			WrapperSlider("Water Depth Color Feather", iSection);
			WrapperSlider("Water Depth Color Floor", iSection);
			WrapperSlider("Undersea Compression", iSection);
			WrapperSlider("Water Color Bottom", iSection);
			WrapperSlider("Water Color Height", iSection);

			WrapperSeparatorText("Fresnel");
			WrapperSlider("Water Fresnel", iSection);

			WrapperSeparatorText("Color Noise");
			WrapperSlider("Water Color Noise Frequency", iSection);
			WrapperSlider("Water Color Noise Amount", iSection);
			WrapperSlider("Water Color Noise Weight One", iSection);
			WrapperSlider("Water Color Noise Multiplier One", iSection);
			WrapperSlider("Water Color Noise Weight Two", iSection);
			WrapperSlider("Water Color Noise Multiplier Two", iSection);

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Beach", iSection, 4))
		{
			WrapperSeparatorText("Breaking");
			WrapperSlider("Break Start Depth", iSection);
			WrapperSlider("Break End Depth", iSection);
			WrapperSlider("Break Blend Curve", iSection);
			WrapperSlider("Medium Shore Softness", iSection);

			WrapperSeparatorText("Skybox Lobe Reduction");
			WrapperSlider("Beach Skybox 1 Reduction", iSection);
			WrapperSlider("Beach Skybox 2 Reduction", iSection);
			WrapperSlider("Beach Skybox 3 Reduction", iSection);

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // BT_CLIENT
