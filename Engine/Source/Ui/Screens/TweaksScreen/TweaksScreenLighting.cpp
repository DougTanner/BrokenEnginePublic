#include "TweaksScreenBase.h"

#if defined(BT_CLIENT)

#include "TweaksSliderMap.h"
#include "Ui/CurveWidget.h"
#include "Ui/LightingWrappersBase.h"

namespace engine
{

namespace
{
const TweaksSliderMapRegistrar gLightingRegistrar
{
	// Write - Pre-Blur
	{"Lighting Blur Sigma", &gLightingBlurSigma},
	{"Lighting Blur Sample Count", &gLightingBlurSampleCount},
	{"Lighting Blur Edge Falloff", &gLightingBlurEdgeFalloff},
	// Write - Deposit
	{"Deposit Texture Multiplier", &gLightingDepositTextureMultiplier},
	{"Deposit Threshold", &gLightingDepositThreshold},
	{"Deposit Compress", &gLightingDepositCompress},
	// Write - Spread
	{"Spread Pass Count", &gSpreadPassCount},
	{"Spread Decay", &gSpreadDecay},
	{"Spread Accumulation Decay", &gSpreadAccumulationDecay},
	// Write - Spread Start
	{"Spread Texture Multiplier Start", &gSpreadTextureMultiplierStart},
	{"Spread Directionality", &gSpreadDirectionality},
	{"Spread Direction Count", &gSpreadDirectionCount},
	{"Spread Distance", &gSpreadDistance},
	{"Spread Ring Count", &gSpreadRingCount},
	{"Spread Jitter", &gSpreadJitter},
	{"Spread Sample Jitter Range Start", &gSpreadSampleJitterRangeStart},
	{"Spread Sample Jitter Clustering Start", &gSpreadSampleJitterClusteringStart},
	{"Spread Distance Falloff", &gSpreadDistanceFalloff},
	{"Spread Height Multiplier", &gSpreadHeightMultiplier},
	{"Spread Height End Height", &gSpreadHeightEndHeight},
	{"Spread Height Power", &gSpreadHeightPower},
	{"Spread Output Threshold", &gSpreadOutputThreshold},
	{"Spread Output Compress", &gSpreadOutputCompress},
	// Write - Spread End
	{"Spread Texture Multiplier End", &gSpreadTextureMultiplierEnd},
	{"Spread Directionality End", &gSpreadDirectionalityEnd},
	{"Spread Direction Count End", &gSpreadDirectionCountEnd},
	{"Spread Distance End Start Height", &gSpreadDistanceEnd.StartHeight},
	{"Spread Distance End End Height", &gSpreadDistanceEnd.EndHeight},
	{"Spread Distance End Low", &gSpreadDistanceEnd.Low},
	{"Spread Distance End High", &gSpreadDistanceEnd.High},
	{"Spread Ring Count End", &gSpreadRingCountEnd},
	{"Spread Jitter End", &gSpreadJitterEnd},
	{"Spread Sample Jitter Range End", &gSpreadSampleJitterRangeEnd},
	{"Spread Sample Jitter Clustering End", &gSpreadSampleJitterClusteringEnd},
	{"Spread Decay End", &gSpreadDecayEnd},
	{"Spread Accumulation Decay End", &gSpreadAccumulationDecayEnd},
	{"Spread Distance Falloff End", &gSpreadDistanceFalloffEnd},
	{"Spread Output Threshold End", &gSpreadOutputThresholdEnd},
	{"Spread Output Compress End", &gSpreadOutputCompressEnd},
	// Write - Temporal
	{"Texel Ramp Speed", &gLightingTexelRampMetersPerSec},
	{"Temporal Blend", &gLightingTemporalBlend},
	// Combine
	{"Combine Max Brightness", &gCombineMaxBrightness},
	{"Combine Contrast", &gCombineContrast},
	{"Combine Linear Start", &gCombineLinearStart},
	{"Combine Linear Length", &gCombineLinearLength},
	{"Combine Toe", &gCombineToe},
	{"Combine Black Tightness", &gCombineBlackTightness},
	{"Combine Pass Normalize", &gCombinePassNormalize},
	{"Combine Exposure Pass Scale", &gCombineExposurePassScale},
	{"Combine Hue Preserve", &gCombineHuePreserve},
	// Read - Terrain Lighting
	{"Directional Intensity", &gLightingDirectionalIntensity},
	{"Directional Power", &gLightingDirectionalPower},
	{"Directional Power Mode", &gLightingDirectionalPowerMode},
	{"Ambient Intensity", &gLightingAmbientIntensity},
	{"Ambient Power", &gLightingAmbientPower},
	{"Ambient Power Mode", &gLightingAmbientPowerMode},
	{"Terrain", &gLightingTerrain},
	{"Terrain Add", &gLightingAddTerrain},
	{"Terrain Below Base Multiplier", &gLightingTerrainBelowBaseMultiplier},
	{"Terrain Below Base Power", &gLightingTerrainBelowBasePower},
	{"Objects", &gLightingObjects},
	{"Objects Add", &gLightingObjectsAdd},
	{"Day Final Multiplier", &gLightingDayFinalMultiplier},
	{"Night Final Multiplier", &gLightingNightFinalMultiplier},
	// Read - Water Lighting
	{"Water EWNS Pow", &gLightingWaterEwnsPow},
	{"Water EWNS Pow Mode", &gLightingWaterEwnsPowMode},
	{"Water Ambient Intensity", &gLightingWaterAmbientIntensity},
	{"Water Ambient Power", &gLightingWaterAmbientPower},
	{"Water Ambient Power Mode", &gLightingWaterAmbientPowerMode},
	{"Water Normal Soften", &gLightingWaterNormalSoften},
	{"Water Normal Blend Wave", &gLightingWaterNormalBlendWave},
	{"Water Intensity", &gLightingWaterIntensity},
	{"Water Add", &gLightingWaterAdd},
	{"Water One", &gLightingWaterOne},
	{"Water One Power", &gLightingWaterOnePower},
	{"Water Two", &gLightingWaterTwo},
	{"Water Two Power", &gLightingWaterTwoPower},
	{"Water Three", &gLightingWaterThree},
	{"Water Three Power", &gLightingWaterThreePower},
	{"Water Power Mode", &gLightingWaterPowerMode},
	// Read - Water Reflected
	{"Water Reflected Amount", &gLightingWaterReflectedAmount},
	{"Water Reflected Normal Blend Wave", &gLightingWaterReflectedNormalBlendWave},
	{"Water Reflected Distortion", &gLightingWaterReflectedDistortion},
	{"Water Reflected Falloff Start", &gLightingWaterReflectedFalloffStart},
	{"Water Reflected Falloff Power", &gLightingWaterReflectedFalloffPower},
	{"Water Reflected Fresnel", &gLightingWaterReflectedFresnel},
	{"Water Reflected Intensity", &gLightingWaterReflectedIntensity},
};
}

void TweaksScreenBase::RenderLightingSection()
{
	static constexpr int64_t kiSection = static_cast<int64_t>(TweakSection::kLighting);

	if (ImGui::BeginTabBar("LightingTabs"))
	{
		if (BeginSubtab("Write", kiSection, 0))
		{
			if (ImGui::BeginTable("LightingWriteColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("1. Pre-Blur");
				WrapperSlider("Sigma", kiSection, 1.0f, "Lighting Blur Sigma");
				WrapperSlider("Sample Count", kiSection, 1.0f, "Lighting Blur Sample Count");
				WrapperSlider("Edge Falloff", kiSection, 1.0f, "Lighting Blur Edge Falloff");

				WrapperSeparatorText("2. Deposit");
				WrapperSlider("Texture Multiplier", kiSection, 1.0f, "Deposit Texture Multiplier");
				WrapperSlider("Threshold", kiSection, 1.0f, "Deposit Threshold");
				WrapperSlider("Compress", kiSection, 1.0f, "Deposit Compress");

				WrapperSeparatorText("3a. Spread");
				WrapperSlider("Pass Count", kiSection, 1.0f, "Spread Pass Count");
				WrapperSlider("Decay", kiSection, 1.0f, "Spread Decay");
				WrapperSlider("Accumulation Decay", kiSection, 1.0f, "Spread Accumulation Decay");

				float fSpreadStartY = ImGui::GetCursorPosY();

				WrapperSeparatorText("3b. Spread Start");
				WrapperSlider("Texture Multiplier Start", kiSection, 1.0f, "Spread Texture Multiplier Start");
				WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality");
				WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count");
				WrapperSlider("Distance", kiSection, 1.0f, "Spread Distance");
				WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count");
				WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter");
				WrapperSlider("Sample Jitter Range", kiSection, 1.0f, "Spread Sample Jitter Range Start");
				WrapperSlider("Sample Jitter Clustering", kiSection, 1.0f, "Spread Sample Jitter Clustering Start");
				WrapperSlider("Distance Falloff", kiSection, 1.0f, "Spread Distance Falloff");
				WrapperSlider("Height Multiplier", kiSection, 1.0f, "Spread Height Multiplier");
				WrapperSlider("Height End Height", kiSection, 1.0f, "Spread Height End Height");
				WrapperSlider("Height Power", kiSection, 1.0f, "Spread Height Power");
				WrapperSlider("Output Threshold", kiSection, 1.0f, "Spread Output Threshold");
				WrapperSlider("Output Compress", kiSection, 1.0f, "Spread Output Compress");

				ImGui::TableNextColumn();

				ImGui::SetCursorPosY(fSpreadStartY);

				WrapperSeparatorText("3c. Spread End");
				WrapperSlider("Texture Multiplier End", kiSection, 1.0f, "Spread Texture Multiplier End");
				WrapperSlider("Directionality", kiSection, 1.0f, "Spread Directionality End");
				WrapperSlider("Direction Count", kiSection, 1.0f, "Spread Direction Count End");
				WrapperSlider("Distance Start Height", kiSection, 1.0f, "Spread Distance End Start Height");
				WrapperSlider("Distance End Height", kiSection, 1.0f, "Spread Distance End End Height");
				WrapperSlider("Distance Low", kiSection, 1.0f, "Spread Distance End Low");
				WrapperSlider("Distance High", kiSection, 1.0f, "Spread Distance End High");
				WrapperSlider("Ring Count", kiSection, 1.0f, "Spread Ring Count End");
				WrapperSlider("Jitter", kiSection, 1.0f, "Spread Jitter End");
				WrapperSlider("Sample Jitter Range", kiSection, 1.0f, "Spread Sample Jitter Range End");
				WrapperSlider("Sample Jitter Clustering", kiSection, 1.0f, "Spread Sample Jitter Clustering End");
				WrapperSlider("Decay", kiSection, 1.0f, "Spread Decay End");
				WrapperSlider("Accumulation Decay", kiSection, 1.0f, "Spread Accumulation Decay End");
				WrapperSlider("Distance Falloff", kiSection, 1.0f, "Spread Distance Falloff End");
				WrapperSlider("Output Threshold", kiSection, 1.0f, "Spread Output Threshold End");
				WrapperSlider("Output Compress", kiSection, 1.0f, "Spread Output Compress End");

				WrapperSeparatorText("4. Temporal");
				WrapperSlider("Texel Ramp Speed", kiSection, 1.0f, "Texel Ramp Speed");
				WrapperSlider("Temporal Blend", kiSection, 1.0f, "Temporal Blend");

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (BeginSubtab("Combine", kiSection, 1))
		{
			if (ImGui::BeginTable("LightingCombineColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Tone Curve");
				WrapperSlider("Max Brightness", kiSection, 1.0f, "Combine Max Brightness");
				WrapperSlider("Contrast", kiSection, 1.0f, "Combine Contrast");
				WrapperSlider("Linear Start", kiSection, 1.0f, "Combine Linear Start");
				WrapperSlider("Linear Length", kiSection, 1.0f, "Combine Linear Length");
				WrapperSlider("Toe", kiSection, 1.0f, "Combine Toe");
				WrapperSlider("Black Tightness", kiSection, 1.0f, "Combine Black Tightness");
				WrapperSlider("Pass Normalize", kiSection, 1.0f, "Combine Pass Normalize");
				WrapperSlider("Exposure Pass Scale", kiSection, 1.0f, "Combine Exposure Pass Scale");
				WrapperSlider("Hue Preserve", kiSection, 1.0f, "Combine Hue Preserve");

				ImGui::TableNextColumn();

				if (ImGui::Button(gbUseCombineCurveNew ? "Using: New Curve" : "Using: Old Curve"))
				{
					gbUseCombineCurveNew = !gbUseCombineCurveNew;
				}

				CurveData& rActiveCurve = gbUseCombineCurveNew ? gCombineCurveNew : gCombineCurveOld;
				if (CurveWidget("Pass Contribution Curve", rActiveCurve))
				{
					mActiveSlider = "Combine Curve";
					miActiveSliderSection = kiSection;
				}

				if (ImGui::Button("Copy Curve To Clipboard"))
				{
					char pBuffer[2048] {};
					const char* pName = gbUseCombineCurveNew ? "gCombineCurveNew" : "gCombineCurveOld";
					int iOffset = std::snprintf(pBuffer, sizeof(pBuffer), "CurveData %s({", pName);
					for (int i = 0; i < rActiveCurve.GetPointCount(); ++i)
					{
						const ImVec2& rPoint = rActiveCurve.GetPoint(i);
						iOffset += std::snprintf(pBuffer + iOffset, sizeof(pBuffer) - iOffset, "%sImVec2(%.4ff, %.4ff)", i == 0 ? "" : ", ", rPoint.x, rPoint.y);
					}
					std::snprintf(pBuffer + iOffset, sizeof(pBuffer) - iOffset, "}, %.4ff, %.4ff);", rActiveCurve.GetYMin(), rActiveCurve.GetYMax());
					ImGui::SetClipboardText(pBuffer);
					LOG(kGraphics, kInfo, "{}", pBuffer);
				}

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Read", kiSection, 2))
		{
			if (ImGui::BeginTable("LightingReadColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Terrain Lighting");
				WrapperSlider("Directional Intensity", kiSection, 1.0f);
				WrapperSlider("Directional Power", kiSection, 1.0f);
				WrapperSlider("Directional Power Mode", kiSection, 1.0f);
				WrapperSlider("Ambient Intensity", kiSection, 1.0f);
				WrapperSlider("Ambient Power", kiSection, 1.0f);
				WrapperSlider("Ambient Power Mode", kiSection, 1.0f);
				WrapperSlider("Terrain", kiSection, 1.0f);
				WrapperSlider("Terrain Add", kiSection, 1.0f);
				WrapperSlider("Below Base Multiplier", kiSection, 1.0f, "Terrain Below Base Multiplier");
				WrapperSlider("Below Base Power", kiSection, 1.0f, "Terrain Below Base Power");
				WrapperSlider("Objects", kiSection, 1.0f);
				WrapperSlider("Objects Add", kiSection, 1.0f);
				WrapperSlider("Day Final Multiplier", kiSection, 1.0f);
				WrapperSlider("Night Final Multiplier", kiSection, 1.0f);

				ImGui::TableNextColumn();

				WrapperSeparatorText("Water Lighting");
				WrapperSlider("Water EWNS Pow", kiSection, 1.0f);
				WrapperSlider("Water EWNS Pow Mode", kiSection, 1.0f);
				WrapperSlider("Water Ambient Intensity", kiSection, 1.0f);
				WrapperSlider("Water Ambient Power", kiSection, 1.0f);
				WrapperSlider("Water Ambient Power Mode", kiSection, 1.0f);
				WrapperSlider("Normal Soften", kiSection, 1.0f, "Water Normal Soften");
				WrapperSlider("Normal Blend Wave", kiSection, 1.0f, "Water Normal Blend Wave");
				WrapperSlider("Intensity", kiSection, 1.0f, "Water Intensity");
				WrapperSlider("Add", kiSection, 1.0f, "Water Add");
				WrapperSlider("One", kiSection, 1.0f, "Water One");
				WrapperSlider("One Power", kiSection, 1.0f, "Water One Power");
				WrapperSlider("Two", kiSection, 1.0f, "Water Two");
				WrapperSlider("Two Power", kiSection, 1.0f, "Water Two Power");
				WrapperSlider("Three", kiSection, 1.0f, "Water Three");
				WrapperSlider("Three Power", kiSection, 1.0f, "Water Three Power");
				WrapperSlider("Power Mode", kiSection, 1.0f, "Water Power Mode");

				WrapperSeparatorText("Water Reflected");
				WrapperSlider("Reflected Amount", kiSection, 1.0f, "Water Reflected Amount");
				WrapperSlider("Reflected Normal Blend Wave", kiSection, 1.0f, "Water Reflected Normal Blend Wave");
				WrapperSlider("Reflected Distortion", kiSection, 1.0f, "Water Reflected Distortion");
				WrapperSlider("Reflected Falloff Start", kiSection, 1.0f, "Water Reflected Falloff Start");
				WrapperSlider("Reflected Falloff Power", kiSection, 1.0f, "Water Reflected Falloff Power");
				WrapperSlider("Reflected Fresnel", kiSection, 1.0f, "Water Reflected Fresnel");
				WrapperSlider("Reflected Intensity", kiSection, 1.0f, "Water Reflected Intensity");

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Visible", kiSection, 3))
		{
			RenderLightingEffectsVisibleTab();
			ImGui::EndTabItem();
		}
		if (BeginSubtab("Lighting", kiSection, 4))
		{
			RenderLightingEffectsLightingTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
