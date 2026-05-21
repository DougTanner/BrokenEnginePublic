#include "WaterWrappersBase.h"

namespace engine
{

// Specular - Normals
// Water normal map atlas: 3 weighted samples each indexed into TextureManager::kpWaterNormalCrcs.
// Index defaults: One=0 (texture "0"), Two=1 (texture "3"), Three=11 (SeaWaves) — must match TextureManager::kiWaterNormalSeaWavesIndex.
// 11 Sea Waves 0.25
// 3 FoamB 0.25
// 6 GeenSeaB 0.15
// 12 SeaWavesB 0.25
Wrapper gWaterNormalIndexOne(int64_t {12}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsOneSize(0.2f, 0.05f, 1.0f);
Wrapper gLightingSampledNormalsWeightOneMin(1.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightOneMax(2.0f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationOne(0.0f, -XM_PI, XM_PI);
// 4 GreenCalm 0.05
// 12 SeaWaves 0.02
// 16 WaterFall 0.04
// 2 Foam 0.03
// 7 Lake 0.06
// 15 Stone and Ripples 
Wrapper gWaterNormalIndexTwo(int64_t {15}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsTwoSize(0.05f, 0.025f, 0.1f);
Wrapper gLightingSampledNormalsWeightTwoMin(0.25f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightTwoMax(0.75f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationTwo(0.75f, -XM_PI, XM_PI);
// 12 SeaWavesB 0.03
// 2 Foam
Wrapper gWaterNormalIndexThree(int64_t {2}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsThreeSize(0.02f, 0.02f, 0.1f);
Wrapper gLightingSampledNormalsWeightThreeMin(0.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightThreeMax(1.25f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationThree(0.2f, -XM_PI, XM_PI);
Wrapper gLightingSampledNormalsSpeedMin(0.02f, 0.0f, 0.1f);
Wrapper gLightingSampledNormalsSpeedMax(0.03f, 0.0f, 0.1f);
Wrapper gWaterDepthReflectionFeather(0.05f, 0.001f, 0.1f);
Wrapper gWaterWaveNormalBlend(0.8f, 0.0f, 1.0f);

// Specular - Skybox
Wrapper gLightingWaterSkyboxSunBias(3.3f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxNormalSoften(0.75f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalBlendWave(0.13f, 0.0f, 0.4f);
Wrapper gLightingWaterSkyboxIntensity(0.0004f, 0.0001f, 0.002f);
Wrapper gLightingWaterSkyboxAdd(2.0f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxOne(2400.0f, 0.0f, 5000.0f);
Wrapper gLightingWaterSkyboxOnePower(250.0f, 50.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwo(150.0f, 0.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwoPower(6.0f, 1.0f, 20.0f);
Wrapper gLightingWaterSkyboxThree(200.0f, 1.0f, 800.0f);
Wrapper gLightingWaterSkyboxThreePower(0.001f, 0.001f, 1.0f);
Wrapper gLightingWaterSkyboxLod(7.5f, 0.0f, 10.0f);
// Resolution multiplier for the WaterSkyboxOne MSAA pre-pass (default 1.0 = framebuffer size).
Wrapper gWaterSkyboxOneRenderMultiplier(1.0f, 0.25f, 2.0f);

// Water - Low/Medium count + High frequency waves
Wrapper gWaterHighMultiplier(0.204f, 0.0f, 0.5f);
Wrapper gWaterHighScaleOne(0.85f, 0.0f, 2.0f);
Wrapper gWaterHighScaleTwo(1.2f, 0.0f, 2.0f);

// Specular - Height Darken
Wrapper gWaterHeightDarkenTop(0.05f, -0.1f, 0.05f);
Wrapper gWaterHeightDarkenBottom(-0.1f, -0.5f, 0.0f);
Wrapper gWaterHeightDarkenTarget(0.2f, 0.0f, 1.0f);
Wrapper gWaterHeightDarkenSource(0.2f, 0.0f, 1.0f);
Wrapper gWaterHeightDarkenLighting(0.1f, 0.0f, 1.0f);

// Low frequency waves
Wrapper gWaterLowCount(31i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gWaterLowMax(255.0f, 0.0f, 255.0f);
Wrapper gWaterLowAngle(4.8f, 0.0f, XM_2PI);
Wrapper gWaterLowWavelength(4.0f, 1.0f, 20.0f);
Wrapper gWaterLowAmplitude(0.05f, 0.0f, 0.1f);
Wrapper gWaterLowSpeed(0.25f, 0.0f, 1.0f);
Wrapper gWaterLowSteepness(0.5f, 0.0f, 2.0f);
Wrapper gWaterLowAngleAdjust(0.2f, 0.0f, 0.5f);
Wrapper gWaterLowWavelengthAdjust(-0.8f, -2.0f, 0.0f);
Wrapper gWaterLowAmplitudeAdjust(1.0f, 0.0f, 4.0f);
Wrapper gWaterLowSpeedAdjust(0.288f, 0.0f, 4.0f);
Wrapper gWaterBeachFadeTop(-0.06f, -0.1f, 0.0f);
Wrapper gWaterBeachFadeBottom(-0.14f, -0.2f, -0.07f);
// Camera-height fade for low-frequency wave amplitudes. At camera eye height ≤ Start, multiplier = 1.0 (full waves). At ≥ End, multiplier = 0.0 (no low waves). Linear in between. Defaults preserve the previous hardcoded 1× → 2× default-eye-height ramp.
Wrapper gWaterLowAmplitudeFadeStart(300.0f, 0.0f, 1000.0f);
Wrapper gWaterLowAmplitudeFadeEnd(500.0f, 0.0f, 1000.0f);

// Medium frequency waves
Wrapper gWaterMediumCount(255i64, std::move(std::vector<int64_t> {15, 31, 63, 127, 255}));
Wrapper gWaterMediumWavelength(2.0f, 0.01f, 10.0f);
Wrapper gWaterMediumAmplitude(0.0f, 0.0f, 0.01f); // 0.002f when active
Wrapper gWaterMediumSpeed(0.1f, 0.001f, 0.5f);
Wrapper gWaterMediumSteepness(2.0f, 0.0f, 10.0f);
Wrapper gWaterMediumAngleAdjust(5.5f, 0.0f, 20.0f);
Wrapper gWaterMediumWavelengthAdjust(0.8f, 0.0f, 1.0f);
Wrapper gWaterMediumAmplitudeAdjust(0.1f, 0.0f, 5.0f);
Wrapper gWaterMediumSpeedAdjust(4.0f, 0.0f, 5.0f);
// Camera-height fade for medium-frequency wave amplitudes (independent of low). Same start/end convention as low.
Wrapper gWaterMediumAmplitudeFadeStart(100.0f, 0.0f, 1000.0f);
Wrapper gWaterMediumAmplitudeFadeEnd(300.0f, 0.0f, 1000.0f);

// Depth
Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
Wrapper gWaterZOffsetTemp(0.0f, -10.0f, 10.0f); // DT: TEMP
Wrapper gWaterTerrainFade(0.8f, 0.1f, 5.0f);
Wrapper gWaterTerrainFadeClamp(0.0f, 0.0f, 0.5f);
Wrapper gWaterHeight(0.0f, -0.1f, 0.03f);
Wrapper gWaterEarlyOut(0.0f, -0.1f, 0.1f);
Wrapper gWaterDepthLutFeather(0.09f, 0.01f, 0.2f);
Wrapper gWaterDepthLutSunsetFadePower(10.0f, 0.1f, 10.0f);
Wrapper gWaterDepthLutSunsetFadeIntensity(0.3f, 0.0f, 1.0f);
Wrapper gWaterDepthColorFeather(0.14f, 0.01f, 0.2f);
Wrapper gWaterDepthColorFloor(0.2f, 0.0f, 1.0f);
Wrapper gWaterUnderseaCompression(1.0f, 0.1f, 1.0f);
Wrapper gWaterColorBottom(0.134f, -2.0f, 2.0f);
Wrapper gWaterColorHeight(1.86f, 0.0f, 4.0f);
Wrapper gWaterFresnel(0.07f, 0.0f, 0.2f); // DT: TEMP
Wrapper gWaterNoiseFrequency(0.007f, 0.0f, 0.02f); // DT: TEMP
Wrapper gWaterNoiseAmount(1.4f, 0.0f, 2.0f); // DT: TEMP
Wrapper gWaterColorNoiseFrequency(0.0013f, 0.0f, 0.01f);
Wrapper gWaterColorNoiseAmount(0.1f, 0.0f, 0.2f);
Wrapper gWaterColorNoiseWeightOne(-1.5f, -2.0f, 2.0f);
// Step 0.1 keeps mult*10 integer for the Water.frag fract()-wrap precision pact (see Water.frag color-noise UV block).
Wrapper gWaterColorNoiseMultiplierOne(0.2f, 0.0f, 1.0f, 0.1f);
Wrapper gWaterColorNoiseWeightTwo(0.5f, -2.0f, 2.0f);
Wrapper gWaterColorNoiseMultiplierTwo(1.0f, 0.0f, 4.0f, 0.1f);

} // namespace engine
