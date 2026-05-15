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
extern Wrapper gWaterGlobalAmplitudeFade;

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

// Low frequency waves
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
extern Wrapper gWaterEarlyOut;
extern Wrapper gWaterDepthLutFeather;
extern Wrapper gWaterDepthColorFeather;
extern Wrapper gWaterFresnel;
extern Wrapper gWaterFresnel2;
extern Wrapper gWaterNoiseFrequency;
extern Wrapper gWaterNoiseAmount;

// Water - Low/Medium count + High frequency waves (radio-button-bound but not via slider map; pure-internal High*)
extern Wrapper gWaterLowCount;
extern Wrapper gWaterMediumCount;
extern Wrapper gWaterHighMultiplier;
extern Wrapper gWaterHighScaleOne;
extern Wrapper gWaterHighScaleTwo;

} // namespace engine
