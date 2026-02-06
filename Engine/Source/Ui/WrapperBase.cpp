#include "WrapperBase.h"

namespace engine
{

Wrapper gFullscreen(true);
Wrapper gPresentMode(VK_PRESENT_MODE_FIFO_KHR, std::move(std::vector<VkPresentModeKHR> {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_LATEST_READY_KHR}));
Wrapper gMultisampling(true);
Wrapper gSampleCount(VK_SAMPLE_COUNT_2_BIT, std::move(std::vector<VkSampleCountFlagBits> {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT}));
Wrapper gAnisotropy(true);
Wrapper gMaxAnisotropy(16.0f, 1.0f, 16.0f);
Wrapper gSampleShading(true);
Wrapper gMinSampleShading(0.6f, 0.0f, 1.0f);
Wrapper gMipLodBias(0.75f, 0.0f, 3.0f);
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

Wrapper gSunAngleOverride(0.1f, 0.0f, XM_PI);
Wrapper gMinimumAmbient(kfDefaultMinimumAmbient, kfDefaultMinimumAmbient, 0.1f);

Wrapper gBaseHeight(6.0f, 0.0f, 20.0f);

Wrapper gMisc0(-50.0f, -60.0f, -40.0f);

// Gltf
Wrapper gGltfExposure(3.0f, 0.0f, 10.0f);
Wrapper gGltfGamma(2.2f, 1.0f, 3.0f);

Wrapper gGltfBrdfDiffuse(12.0f, 0.0f, 20.0f);
Wrapper gGltfBrdfDiffusePower(0.8f, 0.5f, 1.0f);
Wrapper gGltfBrdfSpecular(25.0f, 0.0f, 100.0f);
Wrapper gGltfBrdfSpecularPower(0.35f, 0.1f, 1.0f);
Wrapper gGltfIblAmbient(0.15f, 0.0f, 0.4f);
Wrapper gGltfIbl(2.0f, 0.0f, 3.0f);
Wrapper gGltfIblPower(0.75f, 0.5f, 2.0f);

Wrapper gGltfDayBrightness(3.0f, 1.0f, 4.0f);
Wrapper gGltfSun(0.85f, 0.5f, 1.0f);
Wrapper gGltfSunPower(1.0f, 0.0f, 4.0f);

Wrapper gGltfLightingSpecular(0.5f, 0.0f, 1.0f);
Wrapper gGltfLightingSpecularPower(0.5f, 0.1f, 1.0f);
Wrapper gGltfLighting(0.2f, 0.0f, 0.3f);
Wrapper gGltfLightingPower(1.5f, 0.5f, 2.0f);

Wrapper gGltfSmoke(0.5f, 0.0f, 1.0f);
Wrapper gGltfEmissive(1.0f, 0.0f, 5.0f);

// Sound
Wrapper gMasterVolume(1.0f, 0.0f, 1.0f);
Wrapper gMusicVolume(0.25f, 0.0f, 1.0f);
Wrapper gSoundVolume(1.0f, 0.0f, 1.0f);

// Islands & terrain
Wrapper gVisibleAreaExtraTop(0.046f, 0.0f, 1.0f);
Wrapper gVisibleAreaExtraBottom(0.16f, 0.0f, 1.0f);
Wrapper gIslandHeight(30.0f, 10.0f, 50.0f);
Wrapper gWaterDepth(5.0f, 1.0f, 20.0f);
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
Wrapper gTerrainBeachNormalsSizeOne(0.01f, 0.001f, 0.1f);
Wrapper gTerrainBeachNormalsSizeTwo(0.015f, 0.005f, 0.05f);
Wrapper gTerrainBeachNormalsSizeThree(0.14f, 0.01f, 0.5f);
Wrapper gTerrainBeachNormalsBlend(1.0f, 0.0f, 4.0f);

// Water
Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
Wrapper gWaterTerrainFade(0.025f, 0.001f, 0.04f);
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

// Lighting
Wrapper gLightingTextureMultiplier(1.0f / 4.0f, 1.0f / 64.0f, 1.0f / 1.0f);
Wrapper gLightingBlurDownscale(0.8f, 0.5f, 0.9f);
Wrapper gLightingCombineIndex(0.4f, 0.0f, 10.4f);
Wrapper gLightingBlurDistance(0.1f, 0.0f, 0.5f);
Wrapper gLightingBlurDirectionality(1.0f, 0.0f, 1.0f);
Wrapper gLightingBlurJitter(0.1f, 0.0f, 0.2f);
Wrapper gLightingBlurFirstDivisor(1300.0f, 100.0f, 2000.0f);
Wrapper gLightingBlurDivisor(0.24f, 00.0f, 1.0f);
Wrapper gLightingCombineDecay(0.7f, 0.5f, 1.0f);
Wrapper gLightingCombinePower(0.5f, 0.1f, 2.0f);

Wrapper gLightingDirectional(1.6f, 1.0f, 3.0f);
Wrapper gLightingIndirect(0.8f, 0.5f, 2.0f);
Wrapper gLightingTerrain(0.6f, 0.0f, 2.0f);
Wrapper gLightingAddTerrain(0.3f, 0.0f, 1.0f);
Wrapper gLightingObjects(3.0f, 0.0f, 8.0f);
Wrapper gLightingObjectsAdd(0.2f, 0.0f, 1.0f);

Wrapper gLightingSampledNormalsSize(0.3f, 0.05f, 0.5f);
Wrapper gLightingSampledNormalsSizeMod(0.007f, -0.02f, 0.02f);
Wrapper gLightingSampledNormalsSpeed(0.03f, 0.0f, 0.05f);

Wrapper gLightingTimeOfDayMultiplier(0.5f, 0.0f, 1.0f);

// Water skybox
Wrapper gLightingWaterSkyboxSunBias(2.6f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxNormalSoften(0.7f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalBlendWave(0.08f, 0.0f, 0.2f);
Wrapper gLightingWaterSkyboxIntensity(0.001f, 0.0005f, 0.004f);
Wrapper gLightingWaterSkyboxAdd(0.85f, 0.0f, 2.0f);
Wrapper gLightingWaterSkyboxOne(900.0f, 0.0f, 3000.0f);
Wrapper gLightingWaterSkyboxOnePower(200.0f, 50.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwo(280.0f, 0.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwoPower(8.0f, 2.0f, 10.0f);
Wrapper gLightingWaterSkyboxThree(200.0f, 1.0f, 800.0f);
Wrapper gLightingWaterSkyboxThreePower(1.3f, 0.01f, 2.0f);

// Water specular lighting
Wrapper gLightingWaterSpecularNormalSoften(0.1f, 0.0f, 0.5f);
Wrapper gLightingWaterSpecularNormalBlendWave(0.2f, 0.0f, 0.5f);

Wrapper gLightingWaterSpecularDiffuse(1.5f, 0.0f, 4.0f);
Wrapper gLightingWaterSpecularDirect(9.0f, 0.0f, 40.0f);
Wrapper gLightingWaterSpecular(4.0f, 0.0f, 8.0f);
Wrapper gLightingWaterSpecularIntensity(0.01f, 0.0f, 0.02f);
Wrapper gLightingWaterSpecularAdd(0.6f, 0.0f, 2.0f);
Wrapper gLightingWaterSpecularOne(140.0f, 0.0f, 600.0f);
Wrapper gLightingWaterSpecularOnePower(5.0f, 1.0f, 20.0f);
Wrapper gLightingWaterSpecularTwo(7.0f, 0.0f, 20.0f);
Wrapper gLightingWaterSpecularTwoPower(1.3f, 0.5f, 5.0f);
Wrapper gLightingWaterSpecularThree(4.0f, 0.0f, 10.0f);
Wrapper gLightingWaterSpecularThreePower(0.1f, 0.05f, 0.5f);

// Smoke
Wrapper gSmokeDecay(0.996f, 0.990f, 1.0f);
Wrapper gSmokeDecayExtra(0.99f, 0.95f, 1.0f);
Wrapper gSmokeDecayExtraThreshold(0.00025f, 0.0f, 0.0005f);
Wrapper gSmokeEdgeDecayDistance(0.05f, 0.0f, 1.0f);
Wrapper gSmokeTrailsQuantity(400.0f, 0.0f, 2000.0f);
Wrapper gSmokeTrailsWidthCurrent(0.06f, 0.0f, 0.2f);
Wrapper gSmokeTrailsWidthPrevious(0.015f, 0.0f, 0.2f);
Wrapper gSmokeTrailsLength(1.5f, 0.0f, 10.0f);
Wrapper gSmokeTrailsLengthJitter(1.0f, 0.0f, 10.0f);
Wrapper gSmokeTrailsSideJitter(1.5f, 0.0f, 3.0f);
Wrapper gSmokeTrailsFalloff(3.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailsFollow(0.55f, 0.0f, 1.0f);

Wrapper gSmokeWindNoiseScale(0.06f, 0.001f, 0.1f);
Wrapper gSmokeWindNoiseQuantity(0.00006f, 0.0f, 0.0001f);
Wrapper gSmokeNoiseScaleOne(3.0f, 0.1f, 8.0f);
Wrapper gSmokeNoiseScaleTwo(0.2f, 0.01f, 1.0f);
Wrapper gSmokeNoiseQuantity(0.000055f, 0.00001f, 0.0002f);
Wrapper gSmokeMax(0.1f, 0.0f, 1.0f);
Wrapper gSmokePower(0.18f, 0.1f, 1.0f);
Wrapper gSmokeColorMin(0.45f, 0.0f, 1.0f);
Wrapper gSmokeColorMultiplier(2.0f, 0.1f, 4.0f);
Wrapper gSmokeTrailPower(1.0f, 0.1f, 10.0f);
Wrapper gSmokeTrailAlpha(0.8f, 0.0f, 1.0f);

// Low frequency waves
Wrapper gLowCount(15i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gLowMax(150.0f, 0.0f, 255.0f);
Wrapper gLowAngle(4.95f, 0.0f, XM_2PI);
Wrapper gLowWavelength(2.4f, 1.0f, 20.0f);
Wrapper gLowAmplitude(0.05f, 0.0f, 0.1f);
Wrapper gLowSpeed(0.3f, 0.0f, 1.0f);
Wrapper gLowSteepness(1.0f, 0.0f, 2.0f);

Wrapper gLowAngleAdjust(0.239f, 0.0f, 2.0f);
Wrapper gLowWavelengthAdjust(-0.8f, -2.0f, 0.0f);
Wrapper gLowAmplitudeAdjust(2.0f, 0.0f, 4.0f);
Wrapper gLowSpeedAdjust(2.0f, 0.0f, 4.0f);

// Medium frequency waves
Wrapper gMediumCount(63i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gMediumWavelength(7.5f, 0.01f, 20.0f);
Wrapper gMediumAmplitude(0.0075f, 0.0f, 0.025f);
Wrapper gMediumSpeed(0.2f, 0.001f, 1.0f);
Wrapper gMediumSteepness(3.0f, 0.0f, 10.0f);

Wrapper gMediumAngleAdjust(4.4f, 0.0f, 20.0f);
Wrapper gMediumWavelengthAdjust(0.5f, 0.0f, 10.0f);
Wrapper gMediumAmplitudeAdjust(1.0f, 0.0f, 5.0f);
Wrapper gMediumSpeedAdjust(0.6f, 0.0f, 5.0f);

// High frequency waves & Water
Wrapper gHighMultiplier(0.204f, 0.0f, 0.5f);
Wrapper gHighScaleOne(0.85f, 0.0f, 2.0f);
Wrapper gHighScaleTwo(1.2f, 0.0f, 2.0f);

Wrapper gBeachDirectionalFadeBottom(0.25f, 0.0f, 2.0f);
Wrapper gBeachDirectionalFadeHeight(0.7f, 0.0f, 1.0f);

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

Wrapper gSmokeShadowIntensity(0.6f, 0.0f, 1.0f);

// Hex shield
Wrapper gHexShieldGrow(2.0f, 1.0f, 4.0f);
Wrapper gHexShieldEdgeDistance(18.8f, 18.0f, 19.1f);
Wrapper gHexShieldEdgePower(1.0f, 0.5f, 2.0f);
Wrapper gHexShieldEdgeMultiplier(0.5f, 0.25f, 1.0f);

Wrapper gHexShieldWaveMultiplier(7.0f, 0.0f, 20.0f);
Wrapper gHexShieldWaveDotMultiplier(5.0f, 0.5f, 10.0f);
Wrapper gHexShieldWaveIntensityMultiplier(12.0f, 0.5f, 20.0f);
Wrapper gHexShieldWaveIntensityPower(1.6f, 0.25f, 4.0f);
Wrapper gHexShieldWaveFalloffPower(2.1f, 0.25f, 4.0f);

Wrapper gHexShieldDirectionFalloffPower(4.35f, 2.0f, 10.0f);
Wrapper gHexShieldDirectionMultiplier(4.5f, 0.5f, 8.0f);

// Test
Wrapper gTestOne(0.0f, -10.0f, 10.0f);
Wrapper gTestTwo(0.0f, -10.0f, 10.0f);

} // namespace engine
