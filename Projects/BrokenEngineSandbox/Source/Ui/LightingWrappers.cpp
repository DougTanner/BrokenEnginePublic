#include "LightingWrappers.h"

namespace game
{

// Players - Blasters
engine::Wrapper gPlayersBlasterLightingArea(4.0f, 1.0f, 8.0f);
engine::Wrapper gPlayersBlasterLightingIntensity(1.0f, 0.0f, 2.0f);

engine::Wrapper gPlayerImpactLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gPlayerImpactStartLightingIntensity(0.0f, 0.0f, 2.0f);
engine::Wrapper gPlayerImpactEndLightingIntensity(0.0f, 0.0f, 2.0f);

engine::Wrapper gHexShieldIntensityDecay(1.25f, 0.1f, 6.0f);
engine::Wrapper gHexShieldLightingIntensity(0.0f, 0.0f, 2.0f);

// Explosions - Primary light
engine::Wrapper gExplosionPrimaryLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gExplosionPrimaryLightingIntensity(0.0f, 0.0f, 2.0f);

// Explosions - Secondary light
engine::Wrapper gExplosionSecondaryLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gExplosionSecondaryLightingIntensity(0.0f, 0.0f, 2.0f);

// Explosions - Particle (GPU spark/debris)
engine::Wrapper gExplosionParticleLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gExplosionParticleLightingIntensity(0.0f, 0.0f, 2.0f);

// Blasters - Terrain crater
engine::Wrapper gCraterLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gCraterLightingIntensityStart(2.0f, 0.0f, 2.0f);
engine::Wrapper gCraterLightingIntensityMid(0.5f, 0.0f, 2.0f);
engine::Wrapper gCraterLightingIntensityEnd(0.0f, 0.0f, 2.0f);

// Missiles - Exhaust
engine::Wrapper gMissileExhaustLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gMissileExhaustLightingIntensity(0.0f, 0.0f, 2.0f);

// Spaceships - Explosion
engine::Wrapper gSpaceshipExplosionParticleLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gSpaceshipExplosionLightingIntensity(0.0f, 0.0f, 2.0f);
engine::Wrapper gSpaceshipExplosionParticleLightingIntensity(0.0f, 0.0f, 2.0f);

// Spaceships - Enemy blaster
engine::Wrapper gEnemyBlasterLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gEnemyBlasterLightingIntensity(0.0f, 0.0f, 2.0f);

// Spaceships - Hit flash
engine::Wrapper gHitFlashLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gHitFlashLightingIntensity(0.0f, 0.0f, 2.0f);

// ---------------------------------------------------------------------------
// Visible (on-screen emissive) parameters
// ---------------------------------------------------------------------------

// Explosions - Primary light
engine::Wrapper gExplosionPrimaryVisibleArea(2.0f, 0.1f, 5.0f);
engine::Wrapper gExplosionPrimaryVisibleIntensity(0.6f, 0.05f, 3.0f);

// Explosions - Secondary light
engine::Wrapper gExplosionSecondaryVisibleArea(2.0f, 0.1f, 4.0f);
engine::Wrapper gExplosionSecondaryVisibleIntensity(0.6f, 0.05f, 3.0f);

// Blasters - Terrain crater
engine::Wrapper gCraterVisibleAreaStart(0.35f, 0.05f, 2.0f);
engine::Wrapper gCraterVisibleAreaEnd(0.25f, 0.03f, 1.5f);
engine::Wrapper gCraterVisibleIntensityStart(2.0f, 0.2f, 10.0f);
engine::Wrapper gCraterVisibleIntensityEnd(1.0f, 0.1f, 5.0f);

// Players - Area light
engine::Wrapper gPlayerAreaLightVisibleIntensity(1.0f, 0.1f, 1.0f);

// Players - Impact point light
engine::Wrapper gPlayerImpactVisibleArea(0.675f, 0.07f, 3.5f);
engine::Wrapper gPlayerImpactStartVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gPlayerImpactEndVisibleIntensity(0.5f, 0.05f, 2.5f);

// Missiles - Exhaust
engine::Wrapper gMissileExhaustVisibleIntensity(1.0f, 0.1f, 5.0f);

// Spaceships - Enemy blaster
engine::Wrapper gEnemyBlasterVisibleIntensity(1.0f, 0.1f, 5.0f);

// Spaceships - Hit flash
engine::Wrapper gHitFlashVisibleArea(0.375f, 0.04f, 1.9f);
engine::Wrapper gHitFlashVisibleIntensity(1.0f, 0.1f, 5.0f);

} // namespace game
