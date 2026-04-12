#pragma once

#include "Ui/WrapperBase.h"

namespace game
{

// Explosions - Primary light
extern engine::Wrapper gExpPrimaryVisibleArea;
extern engine::Wrapper gExpPrimaryVisibleIntensity;
extern engine::Wrapper gExpPrimaryLightingArea;
extern engine::Wrapper gExpPrimaryLightingIntensity;

// Explosions - Secondary light
extern engine::Wrapper gExpSecondaryVisibleArea;
extern engine::Wrapper gExpSecondaryVisibleIntensity;
extern engine::Wrapper gExpSecondaryLightingArea;
extern engine::Wrapper gExpSecondaryLightingIntensity;

// Explosions - Primary puff
extern engine::Wrapper gExpPrimaryPuffAreaStart;
extern engine::Wrapper gExpPrimaryPuffAreaEnd;
extern engine::Wrapper gExpPrimaryPuffIntensity;

// Explosions - Secondary puff
extern engine::Wrapper gExpSecondaryPuffAreaStart;
extern engine::Wrapper gExpSecondaryPuffAreaEnd;
extern engine::Wrapper gExpSecondaryPuffIntensity;

// Blasters - Terrain crater
extern engine::Wrapper gCraterVisibleAreaStart;
extern engine::Wrapper gCraterVisibleAreaEnd;
extern engine::Wrapper gCraterVisibleIntensityStart;
extern engine::Wrapper gCraterVisibleIntensityEnd;
extern engine::Wrapper gCraterLightingArea;
extern engine::Wrapper gCraterLightingIntensityStart;
extern engine::Wrapper gCraterLightingIntensityMid;
extern engine::Wrapper gCraterLightingIntensityEnd;

// Blasters - Terrain puff
extern engine::Wrapper gBlasterPuffAreaStart;
extern engine::Wrapper gBlasterPuffAreaEnd;
extern engine::Wrapper gBlasterPuffIntensityStart;
extern engine::Wrapper gBlasterPuffIntensityEnd;

// Players - Area light
extern engine::Wrapper gPlayerAreaLightVisibleIntensity;
extern engine::Wrapper gPlayerAreaLightLightingSize;
extern engine::Wrapper gPlayerAreaLightLightingIntensity;

// Players - Impact point light
extern engine::Wrapper gPlayerImpactStartVisibleArea;
extern engine::Wrapper gPlayerImpactStartVisibleIntensity;
extern engine::Wrapper gPlayerImpactStartLightingArea;
extern engine::Wrapper gPlayerImpactStartLightingIntensity;
extern engine::Wrapper gPlayerImpactEndVisibleIntensity;
extern engine::Wrapper gPlayerImpactEndLightingIntensity;

// Players - Impact puff
extern engine::Wrapper gPlayerImpactPuffAreaStart;
extern engine::Wrapper gPlayerImpactPuffAreaEnd;
extern engine::Wrapper gPlayerImpactPuffIntensityStart;

// Players - Hex shield
extern engine::Wrapper gHexShieldIntensityDecay;
extern engine::Wrapper gHexShieldLightingIntensity;

// Missiles - Exhaust
extern engine::Wrapper gMissileExhaustVisibleIntensity;
extern engine::Wrapper gMissileExhaustLightingArea;
extern engine::Wrapper gMissileExhaustLightingIntensity;

// Missiles - Trail
extern engine::Wrapper gMissileTrailIntensity;

// Spaceships - Explosion
extern engine::Wrapper gSpaceshipExplosionIntensity;
extern engine::Wrapper gSpaceshipExplosionParticleLightingSize;
extern engine::Wrapper gSpaceshipExplosionParticleLightingIntensity;

// Spaceships - Enemy blaster
extern engine::Wrapper gEnemyBlasterVisibleIntensity;
extern engine::Wrapper gEnemyBlasterLightingSize;
extern engine::Wrapper gEnemyBlasterLightingIntensity;

// Spaceships - Hit flash
extern engine::Wrapper gHitFlashStartVisibleArea;
extern engine::Wrapper gHitFlashStartVisibleIntensity;
extern engine::Wrapper gHitFlashStartLightingArea;
extern engine::Wrapper gHitFlashStartLightingIntensity;

} // namespace game
