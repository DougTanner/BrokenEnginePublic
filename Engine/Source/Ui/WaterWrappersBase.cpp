#include "WaterWrappersBase.h"

namespace engine
{

// Specular - Normals
// Water normal map atlas: 3 weighted samples each indexed into TextureManager::kpWaterNormalCrcs.
// 11 Sea Waves 0.25
// 3 FoamB 0.25
// 6 GeenSeaB 0.15
// 12 SeaWavesB 0.25
Wrapper gWaterNormalIndexOne(int64_t {12}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsOneSize(0.2f, 0.05f, 1.0f);
Wrapper gLightingSampledNormalsWeightOneMin(1.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightOneMax(2.0f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationOne(0.16f, -XM_PI, XM_PI);
Wrapper gLightingSampledNormalsSpeedOneMin(0.035f, 0.0f, 0.1f);
Wrapper gLightingSampledNormalsSpeedOneMax(0.15f, 0.0f, 1.0f);
Wrapper gWaterNormalSpeedDirectionOne(-2.3f, -XM_PI, XM_PI);
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
Wrapper gLightingSampledNormalsSpeedTwoMin(0.0f, 0.0f, 0.2f);
Wrapper gLightingSampledNormalsSpeedTwoMax(0.075f, 0.0f, 1.0f);
Wrapper gWaterNormalSpeedDirectionTwo(-1.0f, -XM_PI, XM_PI);
// 12 SeaWavesB 0.03
// 2 Foam
// 4 GreenCalm 0.05
Wrapper gWaterNormalIndexThree(int64_t {4}, std::vector<int64_t> {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
Wrapper gLightingSampledNormalsThreeSize(0.02f, 0.02f, 0.1f);
Wrapper gLightingSampledNormalsWeightThreeMin(0.5f, 0.0f, 4.0f);
Wrapper gLightingSampledNormalsWeightThreeMax(0.8f, 0.0f, 4.0f);
Wrapper gWaterNormalRotationThree(0.17f, -XM_PI, XM_PI);
Wrapper gLightingSampledNormalsSpeedThreeMin(0.1f, 0.0f, 0.2f);
Wrapper gLightingSampledNormalsSpeedThreeMax(0.15f, 0.0f, 1.0f);
Wrapper gWaterNormalSpeedDirectionThree(-1.7f, -XM_PI, XM_PI);
Wrapper gWaterDepthReflectionFeather(0.05f, 0.001f, 0.1f);
Wrapper gWaterWaveNormalBlend(0.8f, 0.0f, 1.0f);

// Specular - Skybox
Wrapper gLightingWaterSkyboxSunBias(3.1f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxNormalSoftenSunrise(0.75f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalSoftenNoon(0.5f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxNormalBlendWave(0.1f, 0.0f, 0.4f);
Wrapper gLightingWaterSkyboxIntensity(0.0003f, 0.0001f, 0.002f);
Wrapper gLightingWaterSkyboxAdd(1.5f, 0.0f, 4.0f);
Wrapper gLightingWaterSkyboxOne(2000.0f, 0.0f, 10000.0f);
Wrapper gLightingWaterSkyboxOnePower(200.0f, 50.0f, 400.0f);
Wrapper gLightingWaterSkyboxTwo(3000.0f, 0.0f, 5000.0f);
Wrapper gLightingWaterSkyboxTwoPower(30.0f, 1.0f, 100.0f);
Wrapper gLightingWaterSkyboxThree(360.0f, 1.0f, 800.0f);
Wrapper gLightingWaterSkyboxThreePower(2.0f, 0.001f, 4.0f);
Wrapper gLightingWaterSkyboxLod(6.5f, 0.0f, 10.0f);
// Specular-AA tuning for Water.frag's WATER_SPEC_AA_MODE variants: variance scales the filter kernel
// (mode 2 default 0.25; mode 1 maps 0.25 -> exact pixel footprint), threshold clamps the widening.
Wrapper gWaterSpecAAVariance(0.0f, 0.0f, 2.0f);
Wrapper gWaterSpecAAThreshold(0.0f, 0.0f, 1.0f);
// WATER_SPEC_AA_MIP_HANDOFF: mip scale multiplies the baked per-mip Toksvig variance term (1.0 = the
// physically-derived kernel); mip bias is the water-normal sampler's LOD bias (negative = sharpen,
// positive = blur; 0 = unbiased, unlike the global -gMipLodBias sharpen), sampler recreate on change.
Wrapper gWaterSpecAAMipScale(0.0f, 0.0f, 4.0f);
Wrapper gWaterNormalMipBias(0.3f, 0.0f, 1.0f);

// Specular - Height Darken
Wrapper gWaterHeightDarkenTop(0.05f, -0.1f, 0.05f);
Wrapper gWaterHeightDarkenBottom(-0.1f, -0.5f, 0.0f);
Wrapper gWaterHeightDarkenTarget(1.0f, 0.0f, 2.0f);
Wrapper gWaterHeightDarkenSource(0.0f, 0.0f, 1.0f);
Wrapper gWaterHeightDarkenLighting(0.1f, 0.0f, 1.0f);

// Low frequency waves
Wrapper gWaterLowCount(int64_t {31}, std::vector<int64_t> {15, 31, 63, 127, 255});
Wrapper gWaterLowMax(255.0f, 0.0f, 255.0f);
Wrapper gWaterLowAngle(4.8f, 0.0f, XM_2PI);
Wrapper gWaterLowWavelength(4.0f, 1.0f, 20.0f);
Wrapper gWaterLowAmplitude(0.05f, 0.0f, 0.1f);
Wrapper gWaterLowSpeed(0.4f, 0.0f, 1.0f);
Wrapper gWaterLowSteepness(0.5f, 0.0f, 2.0f);
Wrapper gWaterLowAngleAdjust(0.2f, 0.0f, 0.5f);
Wrapper gWaterLowWavelengthAdjust(-0.75f, -2.0f, 0.0f);
Wrapper gWaterLowAmplitudeAdjust(1.0f, 0.0f, 4.0f);
Wrapper gWaterLowSpeedAdjust(0.288f, 0.0f, 4.0f);
// Camera-height fade for low-frequency wave amplitudes. At camera eye height ≤ Start, multiplier = 1.0 (full waves). At ≥ End, multiplier = 0.0 (no low waves). Linear in between. Defaults preserve the previous hardcoded 1× → 2× default-eye-height ramp.
Wrapper gWaterLowAmplitudeFadeStart(300.0f, 0.0f, 1000.0f);
Wrapper gWaterLowAmplitudeFadeEnd(600.0f, 0.0f, 1000.0f);

// Medium frequency waves
Wrapper gWaterMediumCount(int64_t {255}, std::vector<int64_t> {15, 31, 63, 127, 255});
Wrapper gWaterMediumWavelength(5.0f, 0.01f, 10.0f);
Wrapper gWaterMediumAmplitude(0.012f, 0.0f, 0.02f);
Wrapper gWaterMediumSpeed(0.2f, 0.001f, 0.3f);
Wrapper gWaterMediumSteepness(1.0f, 0.0f, 2.0f);
Wrapper gWaterMediumAngleAdjust(40.0f, 0.0f, 80.0f);
Wrapper gWaterMediumWavelengthAdjust(0.8f, 0.0f, 1.0f);
Wrapper gWaterMediumAmplitudeAdjust(0.9f, 0.0f, 5.0f);
Wrapper gWaterMediumSpeedAdjust(2.0f, 0.0f, 5.0f);
// Camera-height fade for medium-frequency wave amplitudes (independent of low). Same start/end convention as low.
Wrapper gWaterMediumAmplitudeFadeStart(300.0f, 0.0f, 1000.0f);
Wrapper gWaterMediumAmplitudeFadeEnd(600.0f, 0.0f, 1000.0f);

// Depth
Wrapper gWaterTerrainHeight(3.5f, 1.0f, 8.0f);
Wrapper gWaterZOffsetTemp(0.0f, -10.0f, 10.0f); // DT: TEMP
Wrapper gWaterTerrainFade(0.3f, 0.1f, 5.0f);
Wrapper gWaterTerrainFadeClamp(0.0f, 0.0f, 0.5f);
Wrapper gWaterHeight(0.0f, -0.1f, 0.03f);
Wrapper gWaterEarlyOut(0.0f, -0.1f, 0.1f);
Wrapper gWaterDepthLutFeather(0.1f, 0.01f, 0.2f);
Wrapper gWaterDepthLutSunsetFadePower(10.0f, 0.1f, 10.0f);
Wrapper gWaterDepthLutSunsetFadeIntensity(0.3f, 0.0f, 1.0f);
Wrapper gWaterDepthColorFeather(0.3f, 0.01f, 0.4f);
Wrapper gWaterDepthColorFloor(0.2f, 0.0f, 1.0f);
Wrapper gWaterUnderseaCompression(0.8f, 0.1f, 1.0f);
Wrapper gWaterColorBottom(0.08f, -2.0f, 2.0f);
Wrapper gWaterColorHeight(1.86f, 0.0f, 4.0f);
Wrapper gWaterFresnel(0.07f, 0.0f, 0.2f); // DT: TEMP
Wrapper gWaterColorNoiseFrequency(0.0013f, 0.0f, 0.01f);
Wrapper gWaterColorNoiseAmount(0.1f, 0.0f, 0.2f);
Wrapper gWaterColorNoiseWeightOne(-1.5f, -2.0f, 2.0f);
// Step 0.1 keeps mult*10 integer for the Water.frag fract()-wrap precision contract (see Water.frag color-noise UV block).
Wrapper gWaterColorNoiseMultiplierOne(0.2f, 0.0f, 1.0f, 0.1f);
Wrapper gWaterColorNoiseWeightTwo(0.5f, -2.0f, 2.0f);
Wrapper gWaterColorNoiseMultiplierTwo(1.0f, 0.0f, 4.0f, 0.1f);

// Beach
Wrapper gWaterBreakStartDepth(0.0f, 0.0f, 5.0f);
Wrapper gWaterBreakEndDepth(4.0f, 0.1f, 50.0f);
Wrapper gWaterBreakBlendCurve(0.4f, 0.05f, 1.0f);
Wrapper gWaterMediumShoreSoftness(0.3f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxOneBeachReduction(0.1f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxTwoBeachReduction(0.1f, 0.0f, 1.0f);
Wrapper gLightingWaterSkyboxThreeBeachReduction(0.5f, 0.0f, 1.0f);

} // namespace engine
