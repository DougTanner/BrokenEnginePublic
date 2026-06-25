#pragma once

#include "WrapperBase.h"

namespace engine
{

// Decay
extern Wrapper gSmokeMax;
extern Wrapper gSmokePower;
extern Wrapper gSmokeDecay;
extern Wrapper gSmokeEdgeDecayDistance;

// Color
extern Wrapper gSmokeColorMin;
extern Wrapper gSmokeColorMultiplier;
extern Wrapper gSmokeLightingMultiplier;

// Noise
extern Wrapper gSmokeNoiseScaleOne;
extern Wrapper gSmokeNoiseScaleTwo;
extern Wrapper gSmokeWindNoiseScale;
extern Wrapper gSmokeNoiseQuantity;
extern Wrapper gSmokeWindNoiseQuantity;

// Wind Displacement (Smoke tab's Wind Displacement section)
extern Wrapper gWindToSmokeStrength;
extern Wrapper gWindToSmokePower;
extern Wrapper gWindDisplacementNoiseScale;
extern Wrapper gWindSmokeRetention;
extern Wrapper gWindSmokeAdvection;

// Object
extern Wrapper gSmokeObjectHeight;

// Trails (also rendered in game-side TweaksScreenSmokeDeposits column 3)
extern Wrapper gSmokeTrailsQuantity;
extern Wrapper gSmokeTrailsWidthCurrent;
extern Wrapper gSmokeTrailsWidthPrevious;
extern Wrapper gSmokeTrailsLength;
extern Wrapper gSmokeTrailsLengthJitter;
extern Wrapper gSmokeTrailsSideJitter;
extern Wrapper gSmokeIntensityFalloff;

} // namespace engine
