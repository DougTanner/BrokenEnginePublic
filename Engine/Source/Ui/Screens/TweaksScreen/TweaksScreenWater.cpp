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
	// Specular - Height Darken
	{"Height Darken Top", &gWaterHeightDarkenTop},
	{"Height Darken Bottom", &gWaterHeightDarkenBottom},
	{"Height Darken Clamp", &gWaterHeightDarkenClamp},
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
	// Depth
	{"Water Terrain Height", &gWaterTerrainHeight},
	{"Water Terrain Fade", &gWaterTerrainFade},
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
		if (ImGui::BeginTabItem("Specular", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 0)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
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

				WrapperSeparatorText("Height Darken");
				WrapperSlider("Height Darken Top", kiSection, 1.0f);
				WrapperSlider("Height Darken Bottom", kiSection, 1.0f);
				WrapperSlider("Height Darken Clamp", kiSection, 1.0f);

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Low", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 1)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
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

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Medium", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 2)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
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

			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Depth", nullptr, (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 3) ? ImGuiTabItemFlags_SetSelected : 0))
		{
			if (mApplySubtab[kiSection] && mActiveSubtab[kiSection] == 3)
			{
				mApplySubtab[kiSection] = false;
			}
			if (!mApplySubtab[kiSection])
			{
				mActiveSubtab[kiSection] = 3;
			}
			WrapperSlider("Water Terrain Height", kiSection);
			WrapperSlider("Water Terrain Fade", kiSection);

			WrapperSeparatorText("Color Noise");
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
