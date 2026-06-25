#include "SmokeWrappersBase.h"

namespace engine
{

// Decay
Wrapper gSmokeMax(0.2f, 0.0f, 1.0f);
Wrapper gSmokePower(0.18f, 0.1f, 1.0f);
Wrapper gSmokeDecay(0.998f, 0.990f, 1.0f);
Wrapper gSmokeEdgeDecayDistance(0.05f, 0.0f, 0.1f);

// Color
Wrapper gSmokeColorMin(0.2f, 0.0f, 1.0f);
Wrapper gSmokeColorMultiplier(2.0f, 0.1f, 4.0f);
Wrapper gSmokeLightingMultiplier(1.0f, 0.0f, 2.0f);

// Noise
Wrapper gSmokeNoiseScaleOne(3.0f, 0.1f, 8.0f);
Wrapper gSmokeNoiseScaleTwo(0.2f, 0.01f, 1.0f);
Wrapper gSmokeWindNoiseScale(0.06f, 0.001f, 0.1f);
Wrapper gSmokeNoiseQuantity(0.000055f, 0.00001f, 0.0002f);
Wrapper gSmokeWindNoiseQuantity(0.00006f, 0.0f, 0.0001f);

// Wind Displacement (Smoke tab's Wind Displacement section)
Wrapper gWindToSmokeStrength(0.003f, 0.0f, 0.01f);
Wrapper gWindToSmokePower(1.05f, 0.6f, 2.0f);
Wrapper gWindDisplacementNoiseScale(1.0f, 0.0f, 4.0f);
Wrapper gWindSmokeRetention(0.6f, 0.0f, 1.0f);
Wrapper gWindSmokeAdvection(0.5f, 0.0f, 2.0f);

// Object
Wrapper gSmokeObjectHeight(10.0f, 0.5f, 40.0f);

// Trails (also rendered in game-side TweaksScreenSmokeDeposits column 3)
Wrapper gSmokeTrailsQuantity(800.0f, 0.0f, 2000.0f);
Wrapper gSmokeTrailsWidthCurrent(0.01f, 0.0f, 0.02f);
Wrapper gSmokeTrailsWidthPrevious(0.01f, 0.0f, 0.02f);
Wrapper gSmokeTrailsLength(3.5f, 0.0f, 10.0f);
Wrapper gSmokeTrailsLengthJitter(0.0f, 0.0f, 10.0f);
Wrapper gSmokeTrailsSideJitter(3.0f, 0.0f, 6.0f);
Wrapper gSmokeIntensityFalloff(3.0f, 0.1f, 10.0f);

} // namespace engine
