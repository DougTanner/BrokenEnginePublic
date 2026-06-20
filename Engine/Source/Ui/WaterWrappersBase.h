#pragma once

#include "WrapperBase.h"

namespace engine
{

// Specular - Normals (per-sample row layout: chevron | size | weight-min | weight-max | rotation)
extern Wrapper gWaterNormalIndexOne;
extern Wrapper gLightingSampledNormalsOneSize;
extern Wrapper gLightingSampledNormalsWeightOneMin;
extern Wrapper gLightingSampledNormalsWeightOneMax;
extern Wrapper gWaterNormalRotationOne;
extern Wrapper gWaterNormalIndexTwo;
extern Wrapper gLightingSampledNormalsTwoSize;
extern Wrapper gLightingSampledNormalsWeightTwoMin;
extern Wrapper gLightingSampledNormalsWeightTwoMax;
extern Wrapper gWaterNormalRotationTwo;
extern Wrapper gWaterNormalIndexThree;
extern Wrapper gLightingSampledNormalsThreeSize;
extern Wrapper gLightingSampledNormalsWeightThreeMin;
extern Wrapper gLightingSampledNormalsWeightThreeMax;
extern Wrapper gWaterNormalRotationThree;
extern Wrapper gLightingSampledNormalsSpeedMin;
extern Wrapper gLightingSampledNormalsSpeedMax;
extern Wrapper gWaterDepthReflectionFeather;
extern Wrapper gWaterWaveNormalBlend;

// Specular - Skybox
extern Wrapper gLightingWaterSkyboxSunBias;
extern Wrapper gLightingWaterSkyboxNormalSoften;
extern Wrapper gLightingWaterSkyboxNormalBlendWave;
extern Wrapper gLightingWaterSkyboxIntensity;
extern Wrapper gLightingWaterSkyboxAdd;
extern Wrapper gLightingWaterSkyboxOne;
extern Wrapper gLightingWaterSkyboxOnePower;
extern Wrapper gLightingWaterSkyboxTwo;
extern Wrapper gLightingWaterSkyboxTwoPower;
extern Wrapper gLightingWaterSkyboxThree;
extern Wrapper gLightingWaterSkyboxThreePower;
extern Wrapper gLightingWaterSkyboxLod;
extern Wrapper gWaterSkyboxOneRenderMultiplier;

// High frequency waves (pure-internal, not slider-mapped)
extern Wrapper gWaterHighMultiplier;
extern Wrapper gWaterHighScaleOne;
extern Wrapper gWaterHighScaleTwo;

// Specular - Height Darken
extern Wrapper gWaterHeightDarkenTop;
extern Wrapper gWaterHeightDarkenBottom;
extern Wrapper gWaterHeightDarkenTarget;
extern Wrapper gWaterHeightDarkenSource;
extern Wrapper gWaterHeightDarkenLighting;

// Low frequency waves
extern Wrapper gWaterLowCount; // wave-count radio selector, not slider-mapped
extern Wrapper gWaterLowMax;
extern Wrapper gWaterLowAngle;
extern Wrapper gWaterLowWavelength;
extern Wrapper gWaterLowAmplitude;
extern Wrapper gWaterLowSpeed;
extern Wrapper gWaterLowSteepness;
extern Wrapper gWaterLowAngleAdjust;
extern Wrapper gWaterLowWavelengthAdjust;
extern Wrapper gWaterLowAmplitudeAdjust;
extern Wrapper gWaterLowSpeedAdjust;
extern Wrapper gWaterBeachFadeTop;
extern Wrapper gWaterBeachFadeBottom;
extern Wrapper gWaterLowAmplitudeFadeStart;
extern Wrapper gWaterLowAmplitudeFadeEnd;

// Medium frequency waves
extern Wrapper gWaterMediumCount; // wave-count radio selector, not slider-mapped
extern Wrapper gWaterMediumWavelength;
extern Wrapper gWaterMediumAmplitude;
extern Wrapper gWaterMediumSpeed;
extern Wrapper gWaterMediumSteepness;
extern Wrapper gWaterMediumAngleAdjust;
extern Wrapper gWaterMediumWavelengthAdjust;
extern Wrapper gWaterMediumAmplitudeAdjust;
extern Wrapper gWaterMediumSpeedAdjust;
extern Wrapper gWaterMediumAmplitudeFadeStart;
extern Wrapper gWaterMediumAmplitudeFadeEnd;

// Depth
extern Wrapper gWaterTerrainHeight;
extern Wrapper gWaterZOffsetTemp; // DT: TEMP
extern Wrapper gWaterTerrainFade;
extern Wrapper gWaterTerrainFadeClamp;
extern Wrapper gWaterHeight;
extern Wrapper gWaterEarlyOut;
extern Wrapper gWaterDepthLutFeather;
extern Wrapper gWaterDepthLutSunsetFadePower;
extern Wrapper gWaterDepthLutSunsetFadeIntensity;
extern Wrapper gWaterDepthColorFeather;
extern Wrapper gWaterDepthColorFloor;
extern Wrapper gWaterUnderseaCompression;
extern Wrapper gWaterColorBottom;
extern Wrapper gWaterColorHeight;
extern Wrapper gWaterFresnel;
extern Wrapper gWaterNoiseFrequency;
extern Wrapper gWaterNoiseAmount;
extern Wrapper gWaterColorNoiseFrequency;
extern Wrapper gWaterColorNoiseAmount;
extern Wrapper gWaterColorNoiseWeightOne;
extern Wrapper gWaterColorNoiseMultiplierOne;
extern Wrapper gWaterColorNoiseWeightTwo;
extern Wrapper gWaterColorNoiseMultiplierTwo;

} // namespace engine
