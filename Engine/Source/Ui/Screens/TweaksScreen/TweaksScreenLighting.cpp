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
	{"Lighting Update Cadence", &gLightingUpdateCadence},
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
	const int64_t iSection = giTweakSectionLighting;

	if (ImGui::BeginTabBar("LightingTabs"))
	{
		if (BeginSubtab("Write", iSection, 0))
		{
			if (ImGui::BeginTable("LightingWriteColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("1. Pre-Blur");
				WrapperSlider("Sigma", iSection, 1.0f, "Lighting Blur Sigma");
				WrapperSlider("Sample Count", iSection, 1.0f, "Lighting Blur Sample Count");
				WrapperSlider("Edge Falloff", iSection, 1.0f, "Lighting Blur Edge Falloff");

				WrapperSeparatorText("2. Deposit");
				WrapperSlider("Texture Multiplier", iSection, 1.0f, "Deposit Texture Multiplier");
				WrapperSlider("Threshold", iSection, 1.0f, "Deposit Threshold");
				WrapperSlider("Compress", iSection, 1.0f, "Deposit Compress");

				WrapperSeparatorText("3a. Spread");
				WrapperSlider("Pass Count", iSection, 1.0f, "Spread Pass Count");
				WrapperSlider("Decay", iSection, 1.0f, "Spread Decay");
				WrapperSlider("Accumulation Decay", iSection, 1.0f, "Spread Accumulation Decay");

				float fSpreadStartY = ImGui::GetCursorPosY();

				WrapperSeparatorText("3b. Spread Start");
				WrapperSlider("Texture Multiplier Start", iSection, 1.0f, "Spread Texture Multiplier Start");
				WrapperSlider("Directionality", iSection, 1.0f, "Spread Directionality");
				WrapperSlider("Direction Count", iSection, 1.0f, "Spread Direction Count");
				WrapperSlider("Distance", iSection, 1.0f, "Spread Distance");
				WrapperSlider("Ring Count", iSection, 1.0f, "Spread Ring Count");
				WrapperSlider("Jitter", iSection, 1.0f, "Spread Jitter");
				WrapperSlider("Sample Jitter Range", iSection, 1.0f, "Spread Sample Jitter Range Start");
				WrapperSlider("Sample Jitter Clustering", iSection, 1.0f, "Spread Sample Jitter Clustering Start");
				WrapperSlider("Distance Falloff", iSection, 1.0f, "Spread Distance Falloff");
				WrapperSlider("Height Multiplier", iSection, 1.0f, "Spread Height Multiplier");
				WrapperSlider("Height End Height", iSection, 1.0f, "Spread Height End Height");
				WrapperSlider("Height Power", iSection, 1.0f, "Spread Height Power");
				WrapperSlider("Output Threshold", iSection, 1.0f, "Spread Output Threshold");
				WrapperSlider("Output Compress", iSection, 1.0f, "Spread Output Compress");

				ImGui::TableNextColumn();

				ImGui::SetCursorPosY(fSpreadStartY);

				WrapperSeparatorText("3c. Spread End");
				WrapperSlider("Texture Multiplier End", iSection, 1.0f, "Spread Texture Multiplier End");
				WrapperSlider("Directionality", iSection, 1.0f, "Spread Directionality End");
				WrapperSlider("Direction Count", iSection, 1.0f, "Spread Direction Count End");
				WrapperSlider("Distance Start Height", iSection, 1.0f, "Spread Distance End Start Height");
				WrapperSlider("Distance End Height", iSection, 1.0f, "Spread Distance End End Height");
				WrapperSlider("Distance Low", iSection, 1.0f, "Spread Distance End Low");
				WrapperSlider("Distance High", iSection, 1.0f, "Spread Distance End High");
				WrapperSlider("Ring Count", iSection, 1.0f, "Spread Ring Count End");
				WrapperSlider("Jitter", iSection, 1.0f, "Spread Jitter End");
				WrapperSlider("Sample Jitter Range", iSection, 1.0f, "Spread Sample Jitter Range End");
				WrapperSlider("Sample Jitter Clustering", iSection, 1.0f, "Spread Sample Jitter Clustering End");
				WrapperSlider("Decay", iSection, 1.0f, "Spread Decay End");
				WrapperSlider("Accumulation Decay", iSection, 1.0f, "Spread Accumulation Decay End");
				WrapperSlider("Distance Falloff", iSection, 1.0f, "Spread Distance Falloff End");
				WrapperSlider("Output Threshold", iSection, 1.0f, "Spread Output Threshold End");
				WrapperSlider("Output Compress", iSection, 1.0f, "Spread Output Compress End");

				WrapperSeparatorText("4. Temporal");
				WrapperSlider("Texel Contraction Speed", iSection, 1.0f, "Texel Ramp Speed");
				WrapperSlider("Temporal Blend", iSection, 1.0f, "Temporal Blend");
				WrapperSlider("Update Cadence", iSection, 1.0f, "Lighting Update Cadence");

				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		if (BeginSubtab("Combine", iSection, 1))
		{
			if (ImGui::BeginTable("LightingCombineColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Tone Curve");
				WrapperSlider("Max Brightness", iSection, 1.0f, "Combine Max Brightness");
				WrapperSlider("Contrast", iSection, 1.0f, "Combine Contrast");
				WrapperSlider("Linear Start", iSection, 1.0f, "Combine Linear Start");
				WrapperSlider("Linear Length", iSection, 1.0f, "Combine Linear Length");
				WrapperSlider("Toe", iSection, 1.0f, "Combine Toe");
				WrapperSlider("Black Tightness", iSection, 1.0f, "Combine Black Tightness");
				WrapperSlider("Pass Normalize", iSection, 1.0f, "Combine Pass Normalize");
				WrapperSlider("Exposure Pass Scale", iSection, 1.0f, "Combine Exposure Pass Scale");
				WrapperSlider("Hue Preserve", iSection, 1.0f, "Combine Hue Preserve");

				ImGui::TableNextColumn();

				if (ImGui::Button(gbUseCombineCurveNew ? "Using: New Curve" : "Using: Old Curve"))
				{
					gbUseCombineCurveNew = !gbUseCombineCurveNew;
				}

				CurveData& rActiveCurve = gbUseCombineCurveNew ? gCombineCurveNew : gCombineCurveOld;
				if (CurveWidget("Pass Contribution Curve", rActiveCurve))
				{
					mActiveSlider = "Combine Curve";
					miActiveSliderSection = iSection;
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
		if (BeginSubtab("Read", iSection, 2))
		{
			if (ImGui::BeginTable("LightingReadColumns", 2))
			{
				ImGui::TableNextColumn();

				WrapperSeparatorText("Terrain Lighting");
				WrapperSlider("Directional Intensity", iSection, 1.0f);
				WrapperSlider("Directional Power", iSection, 1.0f);
				WrapperSlider("Directional Power Mode", iSection, 1.0f);
				WrapperSlider("Ambient Intensity", iSection, 1.0f);
				WrapperSlider("Ambient Power", iSection, 1.0f);
				WrapperSlider("Ambient Power Mode", iSection, 1.0f);
				WrapperSlider("Terrain", iSection, 1.0f);
				WrapperSlider("Terrain Add", iSection, 1.0f);
				WrapperSlider("Below Base Multiplier", iSection, 1.0f, "Terrain Below Base Multiplier");
				WrapperSlider("Below Base Power", iSection, 1.0f, "Terrain Below Base Power");
				WrapperSlider("Objects", iSection, 1.0f);
				WrapperSlider("Objects Add", iSection, 1.0f);
				WrapperSlider("Day Final Multiplier", iSection, 1.0f);
				WrapperSlider("Night Final Multiplier", iSection, 1.0f);

				ImGui::TableNextColumn();

				WrapperSeparatorText("Water Lighting");
				WrapperSlider("Water EWNS Pow", iSection, 1.0f);
				WrapperSlider("Water EWNS Pow Mode", iSection, 1.0f);
				WrapperSlider("Water Ambient Intensity", iSection, 1.0f);
				WrapperSlider("Water Ambient Power", iSection, 1.0f);
				WrapperSlider("Water Ambient Power Mode", iSection, 1.0f);
				WrapperSlider("Normal Soften", iSection, 1.0f, "Water Normal Soften");
				WrapperSlider("Normal Blend Wave", iSection, 1.0f, "Water Normal Blend Wave");
				WrapperSlider("Intensity", iSection, 1.0f, "Water Intensity");
				WrapperSlider("Add", iSection, 1.0f, "Water Add");
				WrapperSlider("One", iSection, 1.0f, "Water One");
				WrapperSlider("One Power", iSection, 1.0f, "Water One Power");
				WrapperSlider("Two", iSection, 1.0f, "Water Two");
				WrapperSlider("Two Power", iSection, 1.0f, "Water Two Power");
				WrapperSlider("Three", iSection, 1.0f, "Water Three");
				WrapperSlider("Three Power", iSection, 1.0f, "Water Three Power");
				WrapperSlider("Power Mode", iSection, 1.0f, "Water Power Mode");

				WrapperSeparatorText("Water Reflected");
				WrapperSlider("Reflected Amount", iSection, 1.0f, "Water Reflected Amount");
				WrapperSlider("Reflected Normal Blend Wave", iSection, 1.0f, "Water Reflected Normal Blend Wave");
				WrapperSlider("Reflected Distortion", iSection, 1.0f, "Water Reflected Distortion");
				WrapperSlider("Reflected Falloff Start", iSection, 1.0f, "Water Reflected Falloff Start");
				WrapperSlider("Reflected Falloff Power", iSection, 1.0f, "Water Reflected Falloff Power");
				WrapperSlider("Reflected Fresnel", iSection, 1.0f, "Water Reflected Fresnel");
				WrapperSlider("Reflected Intensity", iSection, 1.0f, "Water Reflected Intensity");

				ImGui::EndTable();
			}

			ImGui::EndTabItem();
		}
		if (BeginSubtab("Visible", iSection, 3))
		{
			RenderLightingEffectsVisibleTab();
			ImGui::EndTabItem();
		}
		if (BeginSubtab("Lighting", iSection, 4))
		{
			RenderLightingEffectsLightingTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
