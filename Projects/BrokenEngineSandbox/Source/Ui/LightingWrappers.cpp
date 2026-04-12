#include "LightingWrappers.h"

namespace game
{

// Explosions - Primary light
engine::Wrapper gExpPrimaryVisibleArea(1.0f, 0.1f, 5.0f);
engine::Wrapper gExpPrimaryVisibleIntensity(0.6f, 0.05f, 3.0f);
engine::Wrapper gExpPrimaryLightingArea(2.0f, 0.2f, 10.0f);
engine::Wrapper gExpPrimaryLightingIntensity(1250.0f, 100.0f, 6000.0f);

// Explosions - Secondary light
engine::Wrapper gExpSecondaryVisibleArea(0.75f, 0.1f, 4.0f);
engine::Wrapper gExpSecondaryVisibleIntensity(0.6f, 0.05f, 3.0f);
engine::Wrapper gExpSecondaryLightingArea(1.5f, 0.15f, 8.0f);
engine::Wrapper gExpSecondaryLightingIntensity(937.5f, 75.0f, 5000.0f);

// Explosions - Primary puff
engine::Wrapper gExpPrimaryPuffAreaStart(0.175f, 0.02f, 1.0f);
engine::Wrapper gExpPrimaryPuffAreaEnd(1.05f, 0.1f, 5.0f);
engine::Wrapper gExpPrimaryPuffIntensity(2.5f, 0.25f, 12.0f);

// Explosions - Secondary puff
engine::Wrapper gExpSecondaryPuffAreaStart(0.175f, 0.02f, 1.0f);
engine::Wrapper gExpSecondaryPuffAreaEnd(0.6f, 0.06f, 3.0f);
engine::Wrapper gExpSecondaryPuffIntensity(0.5f, 0.05f, 2.5f);

// Blasters - Terrain crater
engine::Wrapper gCraterVisibleAreaStart(0.35f, 0.05f, 2.0f);
engine::Wrapper gCraterVisibleAreaEnd(0.25f, 0.03f, 1.5f);
engine::Wrapper gCraterVisibleIntensityStart(2.0f, 0.2f, 10.0f);
engine::Wrapper gCraterVisibleIntensityEnd(1.0f, 0.1f, 5.0f);
engine::Wrapper gCraterLightingArea(1.0f, 0.1f, 5.0f);
engine::Wrapper gCraterLightingIntensityStart(10000.0f, 1000.0f, 50000.0f);
engine::Wrapper gCraterLightingIntensityMid(5000.0f, 500.0f, 25000.0f);
engine::Wrapper gCraterLightingIntensityEnd(2000.0f, 200.0f, 10000.0f);

// Blasters - Terrain puff
engine::Wrapper gBlasterPuffAreaStart(0.25f, 0.03f, 1.25f);
engine::Wrapper gBlasterPuffAreaEnd(0.75f, 0.08f, 3.75f);
engine::Wrapper gBlasterPuffIntensityStart(6.0f, 0.6f, 30.0f);
engine::Wrapper gBlasterPuffIntensityEnd(0.5f, 0.05f, 2.5f);

// Players - Area light
engine::Wrapper gPlayerAreaLightVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gPlayerAreaLightLightingSize(50.0f, 5.0f, 250.0f);
engine::Wrapper gPlayerAreaLightLightingIntensity(100.0f, 10.0f, 500.0f);

// Players - Impact point light
engine::Wrapper gPlayerImpactStartVisibleArea(0.675f, 0.07f, 3.5f);
engine::Wrapper gPlayerImpactStartVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gPlayerImpactStartLightingArea(1.35f, 0.14f, 7.0f);
engine::Wrapper gPlayerImpactStartLightingIntensity(40.0f, 4.0f, 200.0f);
engine::Wrapper gPlayerImpactEndVisibleIntensity(0.5f, 0.05f, 2.5f);
engine::Wrapper gPlayerImpactEndLightingIntensity(10.0f, 1.0f, 50.0f);

// Players - Impact puff
engine::Wrapper gPlayerImpactPuffAreaStart(0.135f, 0.01f, 0.7f);
engine::Wrapper gPlayerImpactPuffAreaEnd(0.45f, 0.05f, 2.25f);
engine::Wrapper gPlayerImpactPuffIntensityStart(4.0f, 0.4f, 20.0f);

// Players - Hex shield
engine::Wrapper gHexShieldIntensityDecay(1.25f, 0.1f, 6.0f);
engine::Wrapper gHexShieldLightingIntensity(125.0f, 10.0f, 625.0f);

// Missiles - Exhaust
engine::Wrapper gMissileExhaustVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gMissileExhaustLightingArea(20.0f, 2.0f, 100.0f);
engine::Wrapper gMissileExhaustLightingIntensity(250.0f, 25.0f, 1250.0f);

// Missiles - Trail
engine::Wrapper gMissileTrailIntensity(0.5f, 0.05f, 2.5f);

// Spaceships - Explosion
engine::Wrapper gSpaceshipExplosionIntensity(1.5f, 0.15f, 7.5f);
engine::Wrapper gSpaceshipExplosionParticleLightingSize(2.25f, 0.2f, 11.0f);
engine::Wrapper gSpaceshipExplosionParticleLightingIntensity(2000.0f, 200.0f, 10000.0f);

// Spaceships - Enemy blaster
engine::Wrapper gEnemyBlasterVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gEnemyBlasterLightingSize(10.0f, 1.0f, 50.0f);
engine::Wrapper gEnemyBlasterLightingIntensity(200.0f, 20.0f, 1000.0f);

// Spaceships - Hit flash
engine::Wrapper gHitFlashStartVisibleArea(0.375f, 0.04f, 1.9f);
engine::Wrapper gHitFlashStartVisibleIntensity(1.0f, 0.1f, 5.0f);
engine::Wrapper gHitFlashStartLightingArea(0.75f, 0.08f, 3.75f);
engine::Wrapper gHitFlashStartLightingIntensity(30.0f, 3.0f, 150.0f);

} // namespace game
