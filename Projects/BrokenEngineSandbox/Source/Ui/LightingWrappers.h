#pragma once

#include "Ui/WrapperBase.h"

namespace game
{

// Explosions - Primary light
extern engine::Wrapper gExplosionPrimaryVisibleArea;
extern engine::Wrapper gExplosionPrimaryVisibleIntensity;
extern engine::Wrapper gExplosionPrimaryLightingArea;
extern engine::Wrapper gExplosionPrimaryLightingIntensity;

// Explosions - Secondary light
extern engine::Wrapper gExplosionSecondaryVisibleArea;
extern engine::Wrapper gExplosionSecondaryVisibleIntensity;
extern engine::Wrapper gExplosionSecondaryLightingArea;
extern engine::Wrapper gExplosionSecondaryLightingIntensity;

// Explosions - Particle
extern engine::Wrapper gExplosionParticleLightingArea;
extern engine::Wrapper gExplosionParticleLightingIntensity;

// Blasters - Terrain crater
extern engine::Wrapper gCraterVisibleAreaStart;
extern engine::Wrapper gCraterVisibleAreaEnd;
extern engine::Wrapper gCraterVisibleIntensityStart;
extern engine::Wrapper gCraterVisibleIntensityEnd;
extern engine::Wrapper gCraterLightingArea;
extern engine::Wrapper gCraterLightingIntensityStart;
extern engine::Wrapper gCraterLightingIntensityMid;
extern engine::Wrapper gCraterLightingIntensityEnd;

// Players - Area light
extern engine::Wrapper gPlayerAreaLightVisibleIntensity;
extern engine::Wrapper gPlayersBlasterLightingArea;
extern engine::Wrapper gPlayersBlasterLightingIntensity;

// Players - Impact point light
extern engine::Wrapper gPlayerImpactVisibleArea;
extern engine::Wrapper gPlayerImpactStartVisibleIntensity;
extern engine::Wrapper gPlayerImpactLightingArea;
extern engine::Wrapper gPlayerImpactStartLightingIntensity;
extern engine::Wrapper gPlayerImpactEndVisibleIntensity;
extern engine::Wrapper gPlayerImpactEndLightingIntensity;

// Players - Hex shield
extern engine::Wrapper gHexShieldIntensityDecay;
extern engine::Wrapper gHexShieldLightingIntensity;

// Missiles - Exhaust
extern engine::Wrapper gMissileExhaustVisibleIntensity;
extern engine::Wrapper gMissileExhaustLightingArea;
extern engine::Wrapper gMissileExhaustLightingIntensity;

// Spaceships - Explosion
extern engine::Wrapper gSpaceshipExplosionLightingIntensity;
extern engine::Wrapper gSpaceshipExplosionParticleLightingArea;
extern engine::Wrapper gSpaceshipExplosionParticleLightingIntensity;

// Spaceships - Enemy blaster
extern engine::Wrapper gEnemyBlasterVisibleIntensity;
extern engine::Wrapper gEnemyBlasterLightingArea;
extern engine::Wrapper gEnemyBlasterLightingIntensity;

// Spaceships - Hit flash
extern engine::Wrapper gHitFlashVisibleArea;
extern engine::Wrapper gHitFlashVisibleIntensity;
extern engine::Wrapper gHitFlashLightingArea;
extern engine::Wrapper gHitFlashLightingIntensity;

} // namespace game
