#include "WaterWrappersBase.h"

namespace engine
{

// Specular - Normals
// Water normal map atlas: 3 weighted samples each indexed into TextureManager::kpWaterNormalCrcs.
// Index defaults: One=0 (texture "0"), Two=1 (texture "3"), Three=11 (SeaWaves) — must match TextureManager::kiWaterNormalSeaWavesIndex.
// 11 Sea Waves 0.25
// 3 FoamB 0.25
// 6 GeenSeaB 0.15
Wrapper gWaterNormalIndexOne(int64_t {6}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsOneSize(0.2f, 0.05f, 1.0f);
Wrapper gLightingSampledNormalsWeightOneMin(2.0f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightOneMax(1.0f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationOne(0.0f, -XM_PI, XM_PI);
// 4 GreenCalm 0.05
// 12 SeaWaves 0.02
// 16 WaterFall 0.04
// 2 Foam 0.03
// 7 Lake 0.06
Wrapper gWaterNormalIndexTwo(int64_t {4}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsTwoSize(0.05f, 0.025f, 0.1f);
Wrapper gLightingSampledNormalsWeightTwoMin(0.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightTwoMax(2.0f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationTwo(-0.15f, -XM_PI, XM_PI);
// 12 SeaWavesB 0.03
Wrapper gWaterNormalIndexThree(int64_t {12}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsThreeSize(0.04f, 0.025f, 0.1f);
Wrapper gLightingSampledNormalsWeightThreeMin(0.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightThreeMax(2.0f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationThree(0.2f, -XM_PI, XM_PI);
Wrapper gLightingSampledNormalsSpeedMin(0.02f, 0.0f, 0.1f);
Wrapper gLightingSampledNormalsSpeedMax(0.05f, 0.0f, 0.1f);
Wrapper gWaterDepthReflectionFeather(0.05f, 0.001f, 0.1f);

// Specular - Skybox
Wrapper gLightingWaterSkyboxSunBias(3.3f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxNormalSoften(0.8f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalBlendWave(0.15f, 0.0f, 0.4f);
Wrapper gLightingWaterSkyboxIntensity(0.0005f, 0.0001f, 0.002f);
Wrapper gLightingWaterSkyboxAdd(2.0f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxOne(2800.0f, 0.0f, 5000.0f);
Wrapper gLightingWaterSkyboxOnePower(250.0f, 50.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwo(100.0f, 0.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwoPower(10.0f, 1.0f, 20.0f);
Wrapper gLightingWaterSkyboxThree(550.0f, 1.0f, 800.0f);
Wrapper gLightingWaterSkyboxThreePower(0.6f, 0.001f, 1.0f);
Wrapper gLightingWaterSkyboxLod(6.0f, 0.0f, 10.0f);

// Specular - Height Darken
Wrapper gWaterHeightDarkenTop(-0.0014f, -0.1f, 0.05f);
Wrapper gWaterHeightDarkenBottom(-0.17f, -0.5f, 0.0f);
Wrapper gWaterHeightDarkenClamp(0.0f, 0.0f, 0.9f);

// Low frequency waves
Wrapper gWaterLowMax(150.0f, 0.0f, 255.0f);
Wrapper gWaterLowAngle(4.87f, 0.0f, XM_2PI);
Wrapper gWaterLowWavelength(2.2f, 1.0f, 20.0f);
Wrapper gWaterLowAmplitude(0.07f, 0.0f, 0.1f);
Wrapper gWaterLowSpeed(0.2f, 0.0f, 1.0f);
Wrapper gWaterLowSteepness(1.0f, 0.0f, 2.0f);
Wrapper gWaterLowAngleAdjust(0.18f, 0.0f, 2.0f);
Wrapper gWaterLowWavelengthAdjust(-0.6f, -2.0f, 0.0f);
Wrapper gWaterLowAmplitudeAdjust(2.0f, 0.0f, 4.0f);
Wrapper gWaterLowSpeedAdjust(1.0f, 0.0f, 4.0f);
Wrapper gWaterBeachFadeTop(-0.06f, -0.1f, 0.0f);
Wrapper gWaterBeachFadeBottom(-0.14f, -0.2f, -0.07f);

// Medium frequency waves
Wrapper gWaterMediumWavelength(7.5f, 0.01f, 20.0f);
Wrapper gWaterMediumAmplitude(0.004f, 0.0f, 0.025f);
Wrapper gWaterMediumSpeed(0.3f, 0.001f, 1.0f);
Wrapper gWaterMediumSteepness(3.0f, 0.0f, 10.0f);
Wrapper gWaterMediumAngleAdjust(4.4f, 0.0f, 20.0f);
Wrapper gWaterMediumWavelengthAdjust(0.6f, 0.0f, 1.0f);
Wrapper gWaterMediumAmplitudeAdjust(1.0f, 0.0f, 5.0f);
Wrapper gWaterMediumSpeedAdjust(0.6f, 0.0f, 5.0f);

// Depth
Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
Wrapper gWaterTerrainFade(0.025f, 0.001f, 1.0f);
Wrapper gWaterColorNoiseWeightOne(-1.5f, -2.0f, 2.0f);
// Step 0.1 keeps mult*10 integer for the Water.frag fract()-wrap precision pact (see Water.frag color-noise UV block).
Wrapper gWaterColorNoiseMultiplierOne(0.2f, 0.0f, 1.0f, 0.1f);
Wrapper gWaterColorNoiseWeightTwo(0.5f, -2.0f, 2.0f);
Wrapper gWaterColorNoiseMultiplierTwo(1.0f, 0.0f, 4.0f, 0.1f);

} // namespace engine
