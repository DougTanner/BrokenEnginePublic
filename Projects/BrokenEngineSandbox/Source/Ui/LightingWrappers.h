#pragma once

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace game
{

// Explosions - Primary light
extern engine::Wrapper gExplosionPrimaryVisibleAreaOne;
extern engine::Wrapper gExplosionPrimaryVisibleAreaTwo;
extern engine::Wrapper gExplosionPrimaryVisibleAreaThree;
extern engine::Wrapper gExplosionPrimaryVisibleIntensityOne;
extern engine::Wrapper gExplosionPrimaryVisibleIntensityTwo;
extern engine::Wrapper gExplosionPrimaryVisibleIntensityThree;
extern engine::Wrapper gExplosionPrimaryLightingAreaOne;
extern engine::Wrapper gExplosionPrimaryLightingAreaTwo;
extern engine::Wrapper gExplosionPrimaryLightingAreaThree;
extern engine::Wrapper gExplosionPrimaryLightingIntensityOne;
extern engine::Wrapper gExplosionPrimaryLightingIntensityTwo;
extern engine::Wrapper gExplosionPrimaryLightingIntensityThree;

// Explosions - Secondary light
extern engine::Wrapper gExplosionSecondaryVisibleAreaOne;
extern engine::Wrapper gExplosionSecondaryVisibleAreaTwo;
extern engine::Wrapper gExplosionSecondaryVisibleAreaThree;
extern engine::Wrapper gExplosionSecondaryVisibleIntensityOne;
extern engine::Wrapper gExplosionSecondaryVisibleIntensityTwo;
extern engine::Wrapper gExplosionSecondaryVisibleIntensityThree;
extern engine::Wrapper gExplosionSecondaryLightingAreaOne;
extern engine::Wrapper gExplosionSecondaryLightingAreaTwo;
extern engine::Wrapper gExplosionSecondaryLightingAreaThree;
extern engine::Wrapper gExplosionSecondaryLightingIntensityOne;
extern engine::Wrapper gExplosionSecondaryLightingIntensityTwo;
extern engine::Wrapper gExplosionSecondaryLightingIntensityThree;

// Blasters - Terrain crater
extern engine::Wrapper gCraterVisibleAreaOne;
extern engine::Wrapper gCraterVisibleAreaTwo;
extern engine::Wrapper gCraterVisibleAreaThree;
extern engine::Wrapper gCraterVisibleAreaFour;
extern engine::Wrapper gCraterVisibleIntensityOne;
extern engine::Wrapper gCraterVisibleIntensityTwo;
extern engine::Wrapper gCraterVisibleIntensityThree;
extern engine::Wrapper gCraterVisibleIntensityFour;
extern engine::Wrapper gCraterLightingAreaOne;
extern engine::Wrapper gCraterLightingAreaTwo;
extern engine::Wrapper gCraterLightingAreaThree;
extern engine::Wrapper gCraterLightingAreaFour;
extern engine::Wrapper gCraterLightingIntensityOne;
extern engine::Wrapper gCraterLightingIntensityTwo;
extern engine::Wrapper gCraterLightingIntensityThree;
extern engine::Wrapper gCraterLightingIntensityFour;

// Players - Area light
extern engine::Wrapper gPlayerAreaLightVisibleIntensity;
extern engine::Wrapper gPlayerBlasterLightingArea;
extern engine::Wrapper gPlayerBlasterLightingIntensity;

// Players - Impact point light
extern engine::Wrapper gPlayerImpactVisibleAreaOne;
extern engine::Wrapper gPlayerImpactVisibleAreaTwo;
extern engine::Wrapper gPlayerImpactVisibleIntensityOne;
extern engine::Wrapper gPlayerImpactVisibleIntensityTwo;
extern engine::Wrapper gPlayerImpactLightingAreaOne;
extern engine::Wrapper gPlayerImpactLightingAreaTwo;
extern engine::Wrapper gPlayerImpactLightingIntensityOne;
extern engine::Wrapper gPlayerImpactLightingIntensityTwo;

// Players - Hex shield
extern engine::Wrapper gHexShieldIntensityDecay;
extern engine::Wrapper gHexShieldLightingIntensity;

// Missiles - Exhaust
extern engine::Wrapper gMissileExhaustVisibleIntensity;
extern engine::Wrapper gMissileExhaustLightingArea;
extern engine::Wrapper gMissileExhaustLightingIntensity;

// Spaceships - Enemy blaster
extern engine::Wrapper gEnemyBlasterVisibleIntensity;
extern engine::Wrapper gEnemyBlasterLightingArea;
extern engine::Wrapper gEnemyBlasterLightingIntensity;

// Spaceships - Hit flash
extern engine::Wrapper gHitFlashVisibleAreaOne;
extern engine::Wrapper gHitFlashVisibleAreaTwo;
extern engine::Wrapper gHitFlashVisibleIntensityOne;
extern engine::Wrapper gHitFlashVisibleIntensityTwo;
extern engine::Wrapper gHitFlashLightingAreaOne;
extern engine::Wrapper gHitFlashLightingAreaTwo;
extern engine::Wrapper gHitFlashLightingIntensityOne;
extern engine::Wrapper gHitFlashLightingIntensityTwo;

} // namespace game

#endif // defined(BT_CLIENT)
