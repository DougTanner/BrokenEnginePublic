#include "LightingWrappersBase.h"

namespace engine
{

// Write - Pre-Blur
Wrapper gLightingBlurSigma(2.0f, 0.01f, 4.0f);
Wrapper gLightingBlurSampleCount(200.0f, 16.0f, 400.0f);
Wrapper gLightingBlurEdgeFalloff(0.4f, 0.01f, 0.6f);

// Write - Deposit
Wrapper gLightingDepositTextureMultiplier(0.4f, 0.2f, 0.8f);
Wrapper gLightingDepositThreshold(0.0f, 0.0f, 4.0f);
Wrapper gLightingDepositCompress(4.0f, 0.0f, 16.0f);

// Write - Spread
Wrapper gSpreadPassCount(40.0f, 1.0f, static_cast<float>(shaders::kiMaxSpreadPasses)); // min >= 1.0f load-bearing: keeps std::pow(fPassCount, -fCombineExposurePassScale) base positive and the 1/fPassCount pass scaling finite (LightingUniforms.cpp).
Wrapper gSpreadDecay(1.0f, 0.0f, 1.0f);
Wrapper gSpreadAccumulationDecay(0.35f, 0.0f, 1.0f);

// Write - Spread Start
Wrapper gSpreadTextureMultiplierStart(0.05f, 0.01f, 0.1f);
Wrapper gSpreadDirectionality(0.1f, 0.0f, 1.0f);
Wrapper gSpreadDirectionCount(4.0f, 4.0f, 32.0f);
Wrapper gSpreadDistance(0.0f, 0.0f, 10.0f);
Wrapper gSpreadRingCount(4.0f, 2.0f, 24.0f); // min >= 2.0f load-bearing: keeps fTotalSamples > 0 so the fNorm divide in LightingSpread.frag:146 can't hit 0/0 (single ring at fDistanceFalloff=1.0 zeroes the only ring).
Wrapper gSpreadJitter(0.0f, 0.0f, 1.0f);
Wrapper gSpreadSampleJitterRangeStart(4.0f, 0.0f, 20.0f);
Wrapper gSpreadSampleJitterClusteringStart(4.0f, 0.25f, 16.0f);
Wrapper gSpreadDistanceFalloff(1.0f, 0.0f, 1.0f);
Wrapper gSpreadHeightMultiplier(0.125f, 0.0f, 0.2f);
Wrapper gSpreadHeightEndHeight(3.0f, 0.1f, 10.0f);
Wrapper gSpreadHeightPower(3.0f, 0.1f, 8.0f);
Wrapper gSpreadOutputThreshold(0.0f, 0.0f, 4.0f);
Wrapper gSpreadOutputCompress(3.5f, 0.0f, 5.0f);

// Write - Spread End
Wrapper gSpreadTextureMultiplierEnd(0.01f, 0.005f, 0.02f);
Wrapper gSpreadDirectionalityEnd(0.8f, 0.0f, 1.0f);
Wrapper gSpreadDirectionCountEnd(4.0f, 4.0f, 16.0f);
HeightLerpWrapperQuartet gSpreadDistanceEnd
{
	Wrapper(150.0f, 0.0f, 1000.0f),  // StartHeight
	Wrapper(600.0f, 0.0f, 1000.0f),  // EndHeight
	Wrapper(10.0f, 1.0f, 40.0f),     // Low
	Wrapper(26.0f, 1.0f, 40.0f),     // High
};
Wrapper gSpreadRingCountEnd(3.0f, 2.0f, 16.0f); // min >= 2.0f load-bearing: see gSpreadRingCount — interpolated ring count must also stay >= 2.
Wrapper gSpreadJitterEnd(0.874f, 0.0f, 1.0f);
Wrapper gSpreadSampleJitterRangeEnd(0.0f, 0.0f, 10.0f);
Wrapper gSpreadSampleJitterClusteringEnd(4.0f, 0.25f, 16.0f);
Wrapper gSpreadDecayEnd(1.0f, 0.0f, 1.0f);
Wrapper gSpreadAccumulationDecayEnd(0.35f, 0.0f, 1.0f);
Wrapper gSpreadDistanceFalloffEnd(0.75f, 0.0f, 1.0f);
Wrapper gSpreadOutputThresholdEnd(0.0f, 0.0f, 4.0f);
Wrapper gSpreadOutputCompressEnd(1.0f, 0.0f, 4.0f);

// Write - Temporal (mirror of the shadow texel-contraction + temporal-blend tunables)
Wrapper gLightingTexelRampMetersPerSec(150.0f, 10.0f, 2000.0f);
Wrapper gLightingTemporalBlend(0.2f, 0.05f, 1.0f);
Wrapper gLightingUpdateCadence(1.0f, 1.0f, 4.0f, 1.0f);

// Combine (Uchimura tone curve)
Wrapper gCombineMaxBrightness(1.3f, 0.1f, 4.0f);
Wrapper gCombineContrast(1.3f, 0.1f, 2.0f); // min > 0 load-bearing: prevents divide-by-zero through `a` in LightCombine.comp:67 / DebugTexture.frag:44.
Wrapper gCombineLinearStart(0.3f, 0.01f, 0.5f);
Wrapper gCombineLinearLength(0.5f, 0.01f, 0.8f);
Wrapper gCombineToe(3.5f, 1.3f, 6.0f);
Wrapper gCombineBlackTightness(0.0f, 0.0f, 0.5f);
Wrapper gCombinePassNormalize(0.7f, 0.0f, 1.0f);
Wrapper gCombineExposurePassScale(0.0f, 0.0f, 1.0f); // Paired with gSpreadPassCount min=1.0f: pow(fPassCount, -scale) stays finite (base>=1, exponent in [-1,0]).
Wrapper gCombineHuePreserve(0.0f, 0.0f, 1.0f);
#if defined(BT_CLIENT)
CurveData gCombineCurveOld({ImVec2(0.0000f, 32.0000f), ImVec2(0.3880f, 25.9852f), ImVec2(0.5766f, 0.9778f), ImVec2(1.0000f, 8.4444f)}, 0.0000f, 32.0000f);
CurveData gCombineCurveNew({ImVec2(0.0000f, 32.0000f), ImVec2(0.3880f, 25.9852f), ImVec2(0.5766f, 0.9778f), ImVec2(1.0000f, 8.4444f)}, 0.0000f, 32.0000f);
bool gbUseCombineCurveNew = true;
#endif

// Read - Terrain Lighting
Wrapper gLightingDirectionalIntensity(0.7f, 0.0f, 2.0f);
Wrapper gLightingDirectionalPower(0.85f, 0.5f, 2.0f);
Wrapper gLightingDirectionalPowerMode(0.0f, 0.0f, 1.0f);
Wrapper gLightingAmbientIntensity(11.0f, 0.0f, 20.0f);
Wrapper gLightingAmbientPower(4.0f, 0.1f, 5.0f);
Wrapper gLightingAmbientPowerMode(1.0f, 0.0f, 1.0f);
Wrapper gLightingTerrain(1.5f, 0.0f, 3.0f);
Wrapper gLightingAddTerrain(0.25f, 0.0f, 0.5f);
Wrapper gLightingTerrainBelowBaseMultiplier(0.7f, 0.0f, 1.0f);
Wrapper gLightingTerrainBelowBasePower(0.3f, 0.1f, 1.0f);
Wrapper gLightingObjects(4.0f, 0.0f, 8.0f);
Wrapper gLightingObjectsAdd(0.2f, 0.0f, 1.0f);
Wrapper gLightingDayFinalMultiplier(1.0f, 0.0f, 1.0f);
Wrapper gLightingNightFinalMultiplier(0.4f, 0.0f, 1.0f);

// Read - Water Lighting
Wrapper gLightingWaterEwnsPow(1.0f, 0.1f, 6.0f);
Wrapper gLightingWaterEwnsPowMode(0.6f, 0.0f, 1.0f);
Wrapper gLightingWaterAmbientIntensity(1.5f, 0.0f, 10.0f);
Wrapper gLightingWaterAmbientPower(2.5f, 0.1f, 5.0f);
Wrapper gLightingWaterAmbientPowerMode(1.0f, 0.0f, 1.0f);
Wrapper gLightingWaterNormalSoften(0.81f, 0.5f, 1.0f);
Wrapper gLightingWaterNormalBlendWave(0.15f, 0.0f, 0.4f);
Wrapper gLightingWaterIntensity(0.04f, 0.0f, 0.1f);
Wrapper gLightingWaterAdd(0.9f, 0.0f, 1.0f);
Wrapper gLightingWaterOne(30.0f, 0.0f, 40.0f);
Wrapper gLightingWaterOnePower(2.5f, 1.0f, 5.0f);
Wrapper gLightingWaterTwo(4.0f, 0.0f, 10.0f);
Wrapper gLightingWaterTwoPower(1.0f, 0.75f, 2.0f);
Wrapper gLightingWaterThree(4.0f, 0.0f, 15.0f);
Wrapper gLightingWaterThreePower(0.5f, 0.25f, 1.0f);
Wrapper gLightingWaterPowerMode(0.8f, 0.0f, 1.0f);

// Read - Water Reflected
Wrapper gLightingWaterReflectedAmount(0.1f, 0.0f, 1.0f);
Wrapper gLightingWaterReflectedNormalBlendWave(0.2f, 0.0f, 0.4f);
Wrapper gLightingWaterReflectedDistortion(23.0f, 0.0f, 40.0f);
Wrapper gLightingWaterReflectedFalloffStart(0.0f, 0.0f, 100.0f);
Wrapper gLightingWaterReflectedFalloffPower(0.6f, 0.1f, 1.0f);
Wrapper gLightingWaterReflectedFresnel(0.9f, 0.0f, 1.0f);
Wrapper gLightingWaterReflectedIntensity(0.4f, 0.0f, 5.0f);

} // namespace engine
