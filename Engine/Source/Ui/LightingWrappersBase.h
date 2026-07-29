#pragma once

#include "HeightLerpWrapperQuartet.h"
#include "WrapperBase.h"

#if defined(BT_CLIENT)
#include "CurveData.h"
#endif

namespace engine
{

// Write - Pre-Blur
extern Wrapper gLightingBlurSigma;
extern Wrapper gLightingBlurSampleCount;
extern Wrapper gLightingBlurEdgeFalloff;

// Write - Deposit
extern Wrapper gLightingDepositTextureMultiplier;
extern Wrapper gLightingDepositThreshold;
extern Wrapper gLightingDepositCompress;

// Write - Spread
extern Wrapper gSpreadPassCount;
extern Wrapper gSpreadDecay;
extern Wrapper gSpreadAccumulationDecay;

// Write - Spread Start
extern Wrapper gSpreadTextureMultiplierStart;
extern Wrapper gSpreadDirectionality;
extern Wrapper gSpreadDirectionCount;
extern Wrapper gSpreadDistance;
extern Wrapper gSpreadRingCount;
extern Wrapper gSpreadJitter;
extern Wrapper gSpreadSampleJitterRangeStart;
extern Wrapper gSpreadSampleJitterClusteringStart;
extern Wrapper gSpreadDistanceFalloff;
extern Wrapper gSpreadHeightMultiplier;
extern Wrapper gSpreadHeightEndHeight;
extern Wrapper gSpreadHeightPower;
extern Wrapper gSpreadOutputThreshold;
extern Wrapper gSpreadOutputCompress;

// Write - Spread End
extern Wrapper gSpreadTextureMultiplierEnd;
extern Wrapper gSpreadDirectionalityEnd;
extern Wrapper gSpreadDirectionCountEnd;
extern HeightLerpWrapperQuartet gSpreadDistanceEnd;  // camera-height-lerped fSpreadDistanceEnd
extern Wrapper gSpreadRingCountEnd;
extern Wrapper gSpreadJitterEnd;
extern Wrapper gSpreadSampleJitterRangeEnd;
extern Wrapper gSpreadSampleJitterClusteringEnd;
extern Wrapper gSpreadDecayEnd;
extern Wrapper gSpreadAccumulationDecayEnd;
extern Wrapper gSpreadDistanceFalloffEnd;
extern Wrapper gSpreadOutputThresholdEnd;
extern Wrapper gSpreadOutputCompressEnd;

// Write - Temporal
extern Wrapper gLightingTexelRampMetersPerSec;
extern Wrapper gLightingTemporalBlend;
extern Wrapper gLightingUpdateCadence;

// Combine (Uchimura tone curve)
extern Wrapper gCombineMaxBrightness;
extern Wrapper gCombineContrast;
extern Wrapper gCombineLinearStart;
extern Wrapper gCombineLinearLength;
extern Wrapper gCombineToe;
extern Wrapper gCombineBlackTightness;
extern Wrapper gCombinePassNormalize;
extern Wrapper gCombineExposurePassScale;
extern Wrapper gCombineHuePreserve;
#if defined(BT_CLIENT)
extern CurveData gCombineCurveOld;
extern CurveData gCombineCurveNew;
extern bool gbUseCombineCurveNew;
#endif

// Read - Terrain Lighting
extern Wrapper gLightingDirectionalIntensity;
extern Wrapper gLightingDirectionalPower;
extern Wrapper gLightingDirectionalPowerMode;
extern Wrapper gLightingAmbientIntensity;
extern Wrapper gLightingAmbientPower;
extern Wrapper gLightingAmbientPowerMode;
extern Wrapper gLightingTerrain;
extern Wrapper gLightingAddTerrain;
extern Wrapper gLightingTerrainBelowBaseMultiplier;
extern Wrapper gLightingTerrainBelowBasePower;
extern Wrapper gLightingObjects;
extern Wrapper gLightingObjectsAdd;
extern Wrapper gLightingDayFinalMultiplier;
extern Wrapper gLightingNightFinalMultiplier;

// Read - Water Lighting
extern Wrapper gLightingWaterEwnsPow;
extern Wrapper gLightingWaterEwnsPowMode;
extern Wrapper gLightingWaterAmbientIntensity;
extern Wrapper gLightingWaterAmbientPower;
extern Wrapper gLightingWaterAmbientPowerMode;
extern Wrapper gLightingWaterNormalSoften;
extern Wrapper gLightingWaterNormalBlendWave;
extern Wrapper gLightingWaterIntensity;
extern Wrapper gLightingWaterAdd;
extern Wrapper gLightingWaterOne;
extern Wrapper gLightingWaterOnePower;
extern Wrapper gLightingWaterTwo;
extern Wrapper gLightingWaterTwoPower;
extern Wrapper gLightingWaterThree;
extern Wrapper gLightingWaterThreePower;
extern Wrapper gLightingWaterPowerMode;

// Read - Water Reflected
extern Wrapper gLightingWaterReflectedAmount;
extern Wrapper gLightingWaterReflectedNormalBlendWave;
extern Wrapper gLightingWaterReflectedDistortion;
extern Wrapper gLightingWaterReflectedFalloffStart;
extern Wrapper gLightingWaterReflectedFalloffPower;
extern Wrapper gLightingWaterReflectedFresnel;
extern Wrapper gLightingWaterReflectedIntensity;

} // namespace engine
