#include "WrapperBase.h"

namespace engine
{

Wrapper gFullscreen(true);
Wrapper gPresentMode(VK_PRESENT_MODE_FIFO_KHR, std::move(std::vector<VkPresentModeKHR> {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_LATEST_READY_KHR}));
Wrapper gMultisampling(true);
Wrapper gSampleCount(VK_SAMPLE_COUNT_4_BIT, std::move(std::vector<VkSampleCountFlagBits> {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT}));
Wrapper gAnisotropy(true);
Wrapper gMaxAnisotropy(16.0f, 1.0f, 16.0f);
Wrapper gSampleShading(true);
Wrapper gMinSampleShading(0.6f, 0.0f, 1.0f);
Wrapper gMipLodBias(1.0f, 0.0f, 2.0f);
Wrapper gFov(45.0f, 25.0f, 110.0f);
Wrapper gWireframe(true);

Wrapper gWorldDetail(1.0f / 8.0f, std::move(std::vector<float> {1.0f / 16.0f, 1.0f / 8.0f, 1.0f / 4.0f})); // Max divisor must match Graphics::WorldDetail()
Wrapper gTerrainElevationTextureMultiplier(0.5f, 0.25f, 1.0f);
Wrapper gTerrainColorTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainNormalTextureMultiplier(2.0f, 1.0f, 3.0f);
Wrapper gTerrainAmbientOcclusionTextureMultiplier(0.5f, 0.25f, 1.0f);
Wrapper gSmoke(true);
Wrapper gSmokeSimulationPixels(1.0f, 0.5f, 1.5f);
Wrapper gSmokeSimulationArea(1.0f, 0.8f, 1.2f);

Wrapper gSunAngleOverride(1.15f, 0.0f, XM_2PI);
Wrapper gMinimumAmbient(0.0f, 0.0f, 0.1f);

Wrapper gBaseHeight(6.0f, 0.0f, 20.0f);

// Pbr - Engine Variables
Wrapper gPbrDayBrightness(3.0f, 1.0f, 4.0f);
Wrapper gPbrSun(0.8f, 0.5f, 1.0f);
Wrapper gPbrSunPower(1.0f, 0.0f, 4.0f);
// Pbr - BRDF
Wrapper gPbrBrdfDiffuse(10.0f, 0.0f, 20.0f);
Wrapper gPbrBrdfDiffusePower(0.8f, 0.5f, 1.0f);
Wrapper gPbrBrdfSpecular(20.0f, 0.0f, 30.0f);
Wrapper gPbrBrdfSpecularPower(0.30f, 0.1f, 1.0f);
// Pbr - Tone Mapping
Wrapper gPbrExposure(3.0f, 0.0f, 10.0f);
Wrapper gPbrGamma(2.2f, 1.0f, 3.0f);
// Pbr - IBL
Wrapper gPbrIblAmbient(0.25f, 0.0f, 0.4f);
Wrapper gPbrIblDiffuse(1.0f, 0.0f, 3.0f);
Wrapper gPbrIblDiffusePower(0.95f, 0.5f, 2.0f);
Wrapper gPbrIblSpecular(4.0f, 0.0f, 6.0f);
Wrapper gPbrIblSpecularPower(1.0f, 0.5f, 2.0f);
Wrapper gPbrIblShadowBlend(0.3f, 0.0f, 1.0f);
Wrapper gPbrIblAmbientColorBlend(0.5f, 0.0f, 1.0f);
Wrapper gPbrCubemapLodPower(1.0f, 0.1f, 1.0f);
Wrapper gPbrCubemapLodOffset(0.0f, 0.0f, 100.0f);
Wrapper gPbrShadowFloor(0.3f, 0.0f, 1.0f);
// Pbr - Post Lighting
Wrapper gPbrLightingSpecular(0.5f, 0.0f, 1.0f);
Wrapper gPbrLightingSpecularPower(0.5f, 0.1f, 1.0f);
Wrapper gPbrLighting(0.2f, 0.0f, 0.3f);
Wrapper gPbrLightingPower(1.5f, 0.5f, 2.0f);
// Pbr - Smoke
Wrapper gPbrSmoke(0.5f, 0.0f, 1.0f);
// Pbr - Emissive
Wrapper gPbrEmissive(1.0f, 0.0f, 5.0f);

// Sound
Wrapper gMasterVolume(1.0f, 0.0f, 1.0f);
Wrapper gMusicVolume(0.25f, 0.0f, 1.0f);
Wrapper gSoundVolume(1.0f, 0.0f, 1.0f);

// Islands & terrain
Wrapper gVisibleAreaExtraTop(0.046f, 0.0f, 1.0f);
Wrapper gVisibleAreaExtraBottom(0.16f, 0.0f, 1.0f);
Wrapper gIslandHeight(30.0f, 10.0f, 50.0f);
Wrapper gWaterDepth(4.4f, 1.0f, 20.0f);
Wrapper gIslandAmbientOcclusion(0.6f, 0.0f, 1.0f);

Wrapper gTerrainEarlyOut(-0.1f, -1.0f, 0.0f);
Wrapper gWaterEarlyOut(0.0f, -0.1f, 0.1f);

Wrapper gTerrainRockMultiplier(3.0f, 1.0f, 20.0f);
Wrapper gTerrainRockSize(0.15f, 0.01f, 0.4f);
Wrapper gTerrainRockBlend(0.6f, 0.0f, 1.0f);
Wrapper gTerrainRockNormalsSizeOne(0.04f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsSizeTwo(0.03f, 0.005f, 0.5f);
Wrapper gTerrainRockNormalsSizeThree(0.25f, 0.01f, 0.5f);
Wrapper gTerrainRockNormalsBlend(0.65f, 0.0f, 2.0f);

Wrapper gTerrainSnowMultiplier(2.4f, 1.0f, 5.0f);

Wrapper gTerrainBeachHeight(0.05f, 0.0f, 0.2f);
Wrapper gTerrainBeachSandSize(0.17f, 0.01f, 0.4f);
Wrapper gTerrainBeachSandBlend(0.6f, 0.0f, 1.0f);
Wrapper gTerrainBeachNormalsSizeOne(0.09f, 0.001f, 0.2f);
Wrapper gTerrainBeachNormalsSizeTwo(0.02f, 0.005f, 0.04f);
Wrapper gTerrainBeachNormalsSizeThree(0.05f, 0.01f, 0.2f);
Wrapper gTerrainBeachNormalsBlend(0.2f, 0.0f, 0.5f);

// Water
Wrapper gWaterHeight(0.0f, -0.1f, 0.03f);
Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
Wrapper gWaterTerrainFade(0.025f, 0.001f, 0.04f);
Wrapper gWaterTerrainFadeClamp(0.0f, 0.0f, 0.5f);
Wrapper gWaterNoiseFrequency(0.007f, 0.0f, 0.02f);
Wrapper gWaterNoiseAmount(1.4f, 0.0f, 2.0f);
Wrapper gWaterColorNoiseFrequency(0.0013f, 0.0f, 0.01f);
Wrapper gWaterColorNoiseAmount(0.1f, 0.0f, 0.2f);

Wrapper gWaterDepthLutFeather(4.0f, 0.01f, 10.0f);
Wrapper gWaterDepthColorFeather(5.2f, 0.1f, 20.0f);
Wrapper gWaterDepthReflectionFeather(0.05f, 0.001f, 0.1f);
Wrapper gWaterFresnel(0.07f, 0.0f, 0.2f);
Wrapper gWaterFresnel2(0.8f, 0.0f, 4.0f);
Wrapper gWaterColorBottom(0.134f, -2.0f, 2.0f);
Wrapper gWaterColorHeight(1.86f, 0.0f, 4.0f);

Wrapper gWaterHeightDarkenTop(-0.0014f, -0.1f, 0.05f);
Wrapper gWaterHeightDarkenBottom(-0.17f, -0.5f, 0.0f);
Wrapper gWaterHeightDarkenClamp(0.0f, 0.0f, 0.9f);

// Lighting Pre-Blur
Wrapper gLightingBlurSigma(2.0f, 0.01f, 4.0f);
Wrapper gLightingBlurSampleCount(200.0f, 16.0f, 400.0f);
Wrapper gLightingBlurEdgeFalloff(0.4f, 0.01f, 0.6f);

// Lighting Deposit
Wrapper gLightingDepositTextureMultiplier(0.5f, 0.1f, 1.0f);
Wrapper gLightingDepositThreshold(0.0f, 0.0f, 4.0f);
Wrapper gLightingDepositCompress(4.0f, 0.0f, 16.0f);

// Lighting Spread
Wrapper gSpreadPassCount(32.0f, 1.0f, static_cast<float>(shaders::kiMaxSpreadPasses)); // min >= 1.0f load-bearing: keeps pow(fPassCount, -fCombineExposurePassScale) base positive in LightCombine.comp:79 / DebugTexture.frag:35.
Wrapper gSpreadDecay(1.0f, 0.0f, 1.0f);
Wrapper gSpreadAccumulationDecay(0.35f, 0.0f, 1.0f);

// Lighting Spread Start
Wrapper gSpreadTextureMultiplierStart(0.2f, 0.05f, 0.2f);
Wrapper gSpreadDirectionality(0.3f, 0.0f, 1.0f);
Wrapper gSpreadDirectionCount(4.0f, 4.0f, 32.0f);
Wrapper gSpreadDistance(0.5f, 0.0f, 10.0f);
Wrapper gSpreadRingCount(4.0f, 1.0f, 24.0f);
Wrapper gSpreadJitter(0.0f, 0.0f, 1.0f);
Wrapper gSpreadDistanceFalloff(1.0f, 0.0f, 1.0f);
Wrapper gSpreadOutputThreshold(0.0f, 0.0f, 4.0f);
Wrapper gSpreadOutputCompress(3.0f, 0.0f, 4.0f);

// Lighting Spread End
Wrapper gSpreadTextureMultiplierEnd(0.01f, 0.01f, 0.1f);
Wrapper gSpreadDirectionalityEnd(0.5f, 0.0f, 1.0f);
Wrapper gSpreadDirectionCountEnd(4.0f, 4.0f, 16.0f);
Wrapper gSpreadDistanceEnd(8.0f, 1.0f, 40.0f);
Wrapper gSpreadRingCountEnd(3.0f, 1.0f, 16.0f);
Wrapper gSpreadJitterEnd(0.874f, 0.0f, 1.0f);
Wrapper gSpreadDistanceFalloffEnd(0.9f, 0.0f, 1.0f);
Wrapper gSpreadOutputThresholdEnd(0.0f, 0.0f, 4.0f);
Wrapper gSpreadOutputCompressEnd(1.0f, 0.0f, 4.0f);

// Lighting Spread Height Fade
Wrapper gSpreadHeightMultiplier(0.125f, 0.0f, 0.2f);
Wrapper gSpreadHeightEndHeight(3.0f, 0.1f, 10.0f);
Wrapper gSpreadHeightPower(3.0f, 0.1f, 8.0f);

// Lighting Combine (Uchimura tone curve)
Wrapper gCombineMaxBrightness(1.3f, 0.1f, 4.0f);
Wrapper gCombineContrast(1.3f, 0.1f, 2.0f); // min > 0 load-bearing: prevents divide-by-zero through `a` in LightCombine.comp:67 / DebugTexture.frag:44.
Wrapper gCombineLinearStart(0.15f, 0.01f, 0.5f);
Wrapper gCombineLinearLength(0.5f, 0.01f, 0.8f);
Wrapper gCombineToe(3.0f, 1.3f, 6.0f);
Wrapper gCombineBlackTightness(0.0f, 0.0f, 0.5f);
Wrapper gCombinePassNormalize(0.7f, 0.0f, 1.0f);
Wrapper gCombineExposurePassScale(0.0f, 0.0f, 1.0f); // Paired with gSpreadPassCount min=1.0f: pow(fPassCount, -scale) stays finite (base>=1, exponent in [-1,0]).
Wrapper gCombineHuePreserve(0.0f, 0.0f, 1.0f);
#if defined(BT_CLIENT)
CurveData gCombineCurveOld({ImVec2(0.0000f, 32.0000f), ImVec2(0.3880f, 25.9852f), ImVec2(0.5766f, 0.9778f), ImVec2(1.0000f, 8.4444f)}, 0.0000f, 32.0000f);
CurveData gCombineCurveNew({ImVec2(0.0000f, 32.0000f), ImVec2(0.3880f, 25.9852f), ImVec2(0.5766f, 0.9778f), ImVec2(1.0000f, 8.4444f)}, 0.0000f, 32.0000f);
bool gbUseCombineCurveNew = true;
#endif

Wrapper gLightingSampledNormalsSize(0.3f, 0.05f, 0.5f);
Wrapper gLightingSampledNormalsSpeed(0.03f, 0.0f, 0.05f);

// New Lighting
Wrapper gLightingNewDirectional(0.8f, 0.0f, 2.0f);
Wrapper gLightingNewDirectionalPower(0.85f, 0.5f, 2.0f);
Wrapper gLightingDirectionalPowerMode(0.0f, 0.0f, 1.0f);
Wrapper gLightingNewAmbient(7.0f, 0.0f, 10.0f);
Wrapper gLightingNewAmbientPower(2.5f, 0.1f, 5.0f);
Wrapper gLightingAmbientPowerMode(1.0f, 0.0f, 1.0f);
Wrapper gLightingTerrain(0.8f, 0.0f, 2.0f);
Wrapper gLightingAddTerrain(0.25f, 0.0f, 0.5f);
Wrapper gLightingTerrainBelowBaseMultiplier(0.7f, 0.0f, 1.0f);
Wrapper gLightingTerrainBelowBasePower(0.3f, 0.1f, 1.0f);
Wrapper gLightingObjects(4.0f, 0.0f, 8.0f);
Wrapper gLightingObjectsAdd(0.2f, 0.0f, 1.0f);
Wrapper gLightingDayFinalMultiplier(1.0f, 0.0f, 1.0f);
Wrapper gLightingNightFinalMultiplier(1.0f, 0.0f, 1.0f);
Wrapper gMoonBrightness(0.1f, 0.0f, 1.0f);

// Water specular lighting
Wrapper gLightingWaterAmbientPower(0.6f, 0.1f, 6.0f);
Wrapper gLightingWaterAmbientPowerMode(0.6f, 0.0f, 1.0f);
Wrapper gLightingWaterNewAmbient(1.5f, 0.0f, 10.0f);
Wrapper gLightingWaterNewAmbientPower(2.5f, 0.1f, 5.0f);
Wrapper gLightingWaterNewAmbientPowerMode(1.0f, 0.0f, 1.0f);
Wrapper gLightingWaterNormalSoften(0.4f, 0.0f, 1.0f);
Wrapper gLightingWaterNormalBlendWave(0.25f, 0.0f, 0.4f);
Wrapper gLightingWaterIntensity(0.035f, 0.0f, 0.1f);
Wrapper gLightingWaterAdd(0.9f, 0.0f, 1.0f);
Wrapper gLightingWaterOne(30.0f, 0.0f, 40.0f);
Wrapper gLightingWaterOnePower(2.5f, 1.0f, 5.0f);
Wrapper gLightingWaterTwo(4.0f, 0.0f, 10.0f);
Wrapper gLightingWaterTwoPower(1.0f, 0.75f, 2.0f);
Wrapper gLightingWaterThree(4.0f, 0.0f, 15.0f);
Wrapper gLightingWaterThreePower(0.5f, 0.25f, 1.0f);
Wrapper gLightingWaterPowerMode(0.8f, 0.0f, 1.0f);

// Water reflected
Wrapper gLightingWaterReflectedAmount(0.1f, 0.0f, 1.0f);
Wrapper gLightingWaterReflectedNormalBlendWave(0.2f, 0.0f, 0.4f);
Wrapper gLightingWaterReflectedDistortion(23.0f, 0.0f, 40.0f);
Wrapper gLightingWaterReflectedFalloffStart(0.0f, 0.0f, 100.0f);
Wrapper gLightingWaterReflectedFalloffPower(0.6f, 0.1f, 1.0f);
Wrapper gLightingWaterReflectedFresnel(0.9f, 0.0f, 1.0f);
Wrapper gLightingWaterReflectedIntensity(0.4f, 0.0f, 5.0f);

// Water skybox
Wrapper gLightingWaterSkyboxSunBias(3.0f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxNormalSoften(0.6f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalBlendWave(0.15f, 0.0f, 0.4f);
Wrapper gLightingWaterSkyboxIntensity(0.001f, 0.0005f, 0.004f);
Wrapper gLightingWaterSkyboxAdd(0.2f, 0.0f, 2.0f);

Wrapper gLightingWaterSkyboxOne(1000.0f, 0.0f, 3000.0f);
Wrapper gLightingWaterSkyboxOnePower(150.0f, 50.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwo(280.0f, 0.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwoPower(4.0f, 2.0f, 10.0f);
Wrapper gLightingWaterSkyboxThree(200.0f, 1.0f, 800.0f);
Wrapper gLightingWaterSkyboxThreePower(2.0f, 0.01f, 2.0f);
Wrapper gLightingWaterSkyboxLod(6.0f, 0.0f, 10.0f);

// Smoke
Wrapper gSmokeDecay(0.996f, 0.990f, 1.0f);
Wrapper gSmokeEdgeDecayDistance(0.05f, 0.0f, 0.1f);
Wrapper gSmokeTrailsQuantity(800.0f, 0.0f, 2000.0f);
Wrapper gSmokeTrailsWidthCurrent(0.01f, 0.0f, 0.02f);
Wrapper gSmokeTrailsWidthPrevious(0.01f, 0.0f, 0.02f);
Wrapper gSmokeTrailsLength(2.5f, 0.0f, 10.0f);
Wrapper gSmokeTrailsLengthJitter(0.0f, 0.0f, 10.0f);
Wrapper gSmokeTrailsSideJitter(3.0f, 0.0f, 6.0f);
Wrapper gSmokeIntensityFalloff(3.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailsFollow(0.55f, 0.0f, 1.0f);

Wrapper gSmokeWindNoiseScale(0.06f, 0.001f, 0.1f);
Wrapper gSmokeWindNoiseQuantity(0.00006f, 0.0f, 0.0001f);
Wrapper gSmokeNoiseScaleOne(3.0f, 0.1f, 8.0f);
Wrapper gSmokeNoiseScaleTwo(0.2f, 0.01f, 1.0f);
Wrapper gSmokeNoiseQuantity(0.000055f, 0.00001f, 0.0002f);
Wrapper gSmokeNoiseInfluence(0.0f, 0.0f, 1000.0f);
Wrapper gSmokeMax(0.2f, 0.0f, 1.0f);
Wrapper gSmokePower(0.18f, 0.1f, 1.0f);
Wrapper gSmokeColorMin(0.2f, 0.0f, 1.0f);
Wrapper gSmokeColorMultiplier(2.0f, 0.1f, 4.0f);
Wrapper gSmokeLightingMultiplier(1.0f, 0.0f, 2.0f);
Wrapper gSmokeTrailPower(1.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailAlpha(0.8f, 0.0f, 1.0f);
Wrapper gSmokeObjectHeight(10.0f, 0.5f, 40.0f);

// Wind - Time & Global
Wrapper gWind(true);
Wrapper gWindTimeScale(0.35f, 0.005f, 0.5f);
Wrapper gWindThresholdLow(0.02f, 0.0f, 0.05f);
Wrapper gWindThresholdHigh(0.05f, 0.05f, 0.1f);
// Wind - Propagation
Wrapper gWindAdvectionScaleHigh(5.0f, 0.5f, 5.0f);
Wrapper gWindAdvectionScaleLow(1.0f, 0.0f, 1.0f);
Wrapper gWindSwirlScaleHigh(0.5f, 0.5f, 4.0f);
Wrapper gWindSwirlScaleLow(1.0f, 0.5f, 3.0f);
Wrapper gWindSwirlAmountHigh(110.0f, 0.0f, 500.0f);
Wrapper gWindSwirlAmountLow(90.0f, 0.0f, 100.0f);
Wrapper gWindSwirlSpeedHigh(2.0f, 0.0f, 10.0f);
Wrapper gWindSwirlSpeedLow(5.0f, 0.0f, 10.0f);
Wrapper gWindVorticityConfinementHigh(4.0f, 0.0f, 4.0f);
Wrapper gWindVorticityConfinementLow(0.5f, 0.0f, 1.0f);
Wrapper gWindDecayHigh(0.99f, 0.5f, 0.999f); // max < 1.0f load-bearing: keeps fDecayRate = 1 - mix(low, high, t) > 0 in WindSpreadCommon.h:70.
Wrapper gWindDecayLow(0.4f, 0.0f, 0.9f); // max < 1.0f load-bearing: keeps fDecayRate = 1 - mix(low, high, t) > 0 in WindSpreadCommon.h:70.
Wrapper gWindMomentumHigh(0.6f, 0.0f, 1.0f);
Wrapper gWindMomentumLow(0.75f, 0.0f, 1.0f);
Wrapper gWindDiffusionHigh(0.0f, 0.0f, 100.0f);
Wrapper gWindDiffusionLow(100.0f, 0.0f, 100.0f);
// Wind - Integration
Wrapper gWindToSmokeStrength(0.003f, 0.0f, 0.01f);
Wrapper gWindSmokeRetention(0.6f, 0.0f, 1.0f);
Wrapper gWindToSmokePower(1.05f, 0.6f, 2.0f);
// Wind - Displacement
Wrapper gWindDisplacementNoiseScale(1.0f, 0.0f, 4.0f);
Wrapper gWindSmokeAdvection(0.5f, 0.0f, 2.0f);

// Low frequency waves
Wrapper gLowCount(15i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gLowMax(150.0f, 0.0f, 255.0f);
Wrapper gLowAngle(4.95f, 0.0f, XM_2PI);
Wrapper gLowWavelength(2.2f, 1.0f, 20.0f);
Wrapper gLowAmplitude(0.05f, 0.0f, 0.1f);
Wrapper gLowSpeed(0.2f, 0.0f, 1.0f);
Wrapper gLowSteepness(1.0f, 0.0f, 2.0f);

Wrapper gLowAngleAdjust(0.239f, 0.0f, 2.0f);
Wrapper gLowWavelengthAdjust(-0.6f, -2.0f, 0.0f);
Wrapper gLowAmplitudeAdjust(2.0f, 0.0f, 4.0f);
Wrapper gLowSpeedAdjust(1.0f, 0.0f, 4.0f);

// Medium frequency waves
Wrapper gMediumCount(63i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gMediumWavelength(7.5f, 0.01f, 20.0f);
Wrapper gMediumAmplitude(0.0075f, 0.0f, 0.025f);
Wrapper gMediumSpeed(0.3f, 0.001f, 1.0f);
Wrapper gMediumSteepness(3.0f, 0.0f, 10.0f);

Wrapper gMediumAngleAdjust(4.4f, 0.0f, 20.0f);
Wrapper gMediumWavelengthAdjust(0.5f, 0.0f, 10.0f);
Wrapper gMediumAmplitudeAdjust(1.0f, 0.0f, 5.0f);
Wrapper gMediumSpeedAdjust(0.6f, 0.0f, 5.0f);

// High frequency waves & Water
Wrapper gHighMultiplier(0.204f, 0.0f, 0.5f);
Wrapper gHighScaleOne(0.85f, 0.0f, 2.0f);
Wrapper gHighScaleTwo(1.2f, 0.0f, 2.0f);

Wrapper gBeachFadeTop(-0.06f, -0.1f, 0.0f);
Wrapper gBeachFadeBottom(-0.14f, -0.2f, -0.07f);

// Shadow
Wrapper gShadowFeatherNoon(2.5f, 0.0f, 8.0f);
Wrapper gShadowFeatherNoonOffset(0.92f, 0.0f, 5.0f);
Wrapper gShadowFeatherSunset(0.05f, 0.0f, 0.5f);
Wrapper gShadowFeatherSunsetOffset(-0.1f, -0.5f, 0.1f);
Wrapper gShadowFeatherPower(1.7f, 0.1f, 10.0f);
Wrapper gShadowDistanceFallof(120.0f, 10.0f, 400.0f);
Wrapper gShadowBlurSigma(9.0f, 1.0f, 20.0f);
Wrapper gShadowAffectAmbient(0.75f, 0.0f, 1.0f);
Wrapper gShadowHeightFadeTop(4.0f, 0.0f, 20.0f);
Wrapper gShadowHeightFadeBottom(0.0f, -20.0f, 0.0f);

Wrapper gObjectShadowsRenderMultiplier(2.0f, 0.25f, 4.0f);
Wrapper gObjectShadowsBlurMultiplier(0.5f, 0.125f, 1.0f);
Wrapper gObjectShadowsNoon(0.6f, 0.1f, 1.0f);
Wrapper gObjectShadowsSunset(0.4f, 0.01f, 1.0f);
Wrapper gObjectShadowsSunsetStretch(2.5f, 0.0f, 10.0f);
Wrapper gObjectShadowsBlurDistanceNoon(0.0001f, 0.00005f, 0.002f);
Wrapper gObjectShadowsBlurDistanceSunset(0.0008f, 0.0001f, 0.004f);
Wrapper gObjectShadowsBlurSigma(5.0f, 1.0f, 20.0f);

Wrapper gSmokeShadowIntensity(0.5f, 0.0f, 1.0f);

// Particles

// Opaque UI
Wrapper gOpaqueUi(false);
Wrapper gUiOpacity(0.9f, 0.0f, 1.0f);

// Font Scale
Wrapper gUiFontScale(1.0f, 0.5f, 3.0f);

// Debug
Wrapper gDebugTexture(false);
Wrapper gDebugTextureIndex(0.0f, 0.0f, static_cast<float>(shaders::kiMaxDebugTextures - 1));
Wrapper gDebugTextureLinearRange(1.0f, 0.1f, 10.0f);

// Test
Wrapper gTestOne(0.0f, -10.0f, 10.0f);
Wrapper gTestTwo(0.0f, -10.0f, 10.0f);

} // namespace engine
