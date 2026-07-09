#if defined(BT_CLIENT)

#include "LightingWrappers.h"

namespace game
{

// ============================================================================
// Lighting (scene light contribution)
// ============================================================================

// Explosions - Primary light
engine::Wrapper gExplosionPrimaryLightingAreaOne(16.0f, 1.0f, 32.0f);
engine::Wrapper gExplosionPrimaryLightingAreaTwo(8.0f, 1.0f, 16.0f);
engine::Wrapper gExplosionPrimaryLightingAreaThree(0.0f, 0.0f, 4.0f);
engine::Wrapper gExplosionPrimaryLightingIntensityOne(0.6f, 0.0f, 1.0f);
engine::Wrapper gExplosionPrimaryLightingIntensityTwo(0.15f, 0.0f, 1.0f);
engine::Wrapper gExplosionPrimaryLightingIntensityThree(0.0f, 0.0f, 2.0f);

// Explosions - Secondary light
engine::Wrapper gExplosionSecondaryLightingAreaOne(8.0f, 0.0f, 16.0f);
engine::Wrapper gExplosionSecondaryLightingAreaTwo(2.0f, 1.0f, 4.0f);
engine::Wrapper gExplosionSecondaryLightingAreaThree(0.0f, 0.0f, 4.0f);
engine::Wrapper gExplosionSecondaryLightingIntensityOne(0.5f, 0.0f, 1.0f);
engine::Wrapper gExplosionSecondaryLightingIntensityTwo(0.1f, 0.0f, 1.0f);
engine::Wrapper gExplosionSecondaryLightingIntensityThree(0.0f, 0.0f, 2.0f);

// Blasters - Terrain crater
engine::Wrapper gCraterLightingAreaOne(8.0f, 1.0f, 16.0f);
engine::Wrapper gCraterLightingAreaTwo(2.0f, 1.0f, 4.0f);
engine::Wrapper gCraterLightingAreaThree(1.0f, 1.0f, 4.0f);
engine::Wrapper gCraterLightingAreaFour(0.0f, 0.0f, 4.0f);
engine::Wrapper gCraterLightingIntensityOne(0.3f, 0.0f, 1.0f);
engine::Wrapper gCraterLightingIntensityTwo(0.1f, 0.0f, 2.0f);
engine::Wrapper gCraterLightingIntensityThree(0.05f, 0.0f, 2.0f);
engine::Wrapper gCraterLightingIntensityFour(0.0f, 0.0f, 2.0f);

// Players - Area light
engine::Wrapper gPlayerBlasterLightingArea(3.0f, 1.0f, 8.0f);
engine::Wrapper gPlayerBlasterLightingIntensity(2.5f, 0.0f, 6.0f);

// Players - Impact point light
engine::Wrapper gPlayerImpactLightingAreaOne(2.0f, 1.0f, 4.0f);
engine::Wrapper gPlayerImpactLightingAreaTwo(0.0f, 0.0f, 4.0f);
engine::Wrapper gPlayerImpactLightingIntensityOne(0.0f, 0.0f, 2.0f);
engine::Wrapper gPlayerImpactLightingIntensityTwo(0.0f, 0.0f, 2.0f);

// Players - Hex shield
engine::Wrapper gHexShieldLightingIntensity(4.0f, 0.0f, 8.0f);

// Missiles - Exhaust
engine::Wrapper gMissileExhaustLightingArea(2.0f, 1.0f, 4.0f);
engine::Wrapper gMissileExhaustLightingIntensity(3.5f, 0.0f, 5.0f);

// Spaceships - Enemy blaster
engine::Wrapper gEnemyBlasterLightingArea(1.0f, 0.5f, 2.0f);
engine::Wrapper gEnemyBlasterLightingIntensity(0.2f, 0.0f, 1.0f);

// Spaceships - Hit flash
engine::Wrapper gHitFlashLightingAreaOne(2.0f, 1.0f, 4.0f);
engine::Wrapper gHitFlashLightingAreaTwo(0.0f, 0.0f, 4.0f);
engine::Wrapper gHitFlashLightingIntensityOne(0.0f, 0.0f, 2.0f);
engine::Wrapper gHitFlashLightingIntensityTwo(0.0f, 0.0f, 2.0f);

// ============================================================================
// Visible (visual area / intensity)
// ============================================================================

// Explosions - Primary light
engine::Wrapper gExplosionPrimaryVisibleAreaOne(4.0f, 0.1f, 5.0f);
engine::Wrapper gExplosionPrimaryVisibleAreaTwo(2.0f, 0.1f, 5.0f);
engine::Wrapper gExplosionPrimaryVisibleAreaThree(0.0f, 0.0f, 5.0f);
engine::Wrapper gExplosionPrimaryVisibleIntensityOne(3.0f, 0.0f, 3.0f);
engine::Wrapper gExplosionPrimaryVisibleIntensityTwo(1.0f, 0.0f, 3.0f);
engine::Wrapper gExplosionPrimaryVisibleIntensityThree(0.0f, 0.0f, 3.0f);

// Explosions - Secondary light
engine::Wrapper gExplosionSecondaryVisibleAreaOne(2.0f, 0.0f, 4.0f);
engine::Wrapper gExplosionSecondaryVisibleAreaTwo(0.5f, 0.1f, 4.0f);
engine::Wrapper gExplosionSecondaryVisibleAreaThree(0.0f, 0.0f, 4.0f);
engine::Wrapper gExplosionSecondaryVisibleIntensityOne(0.5f, 0.0f, 3.0f);
engine::Wrapper gExplosionSecondaryVisibleIntensityTwo(0.25f, 0.0f, 3.0f);
engine::Wrapper gExplosionSecondaryVisibleIntensityThree(0.0f, 0.0f, 3.0f);

// Blasters - Terrain crater
engine::Wrapper gCraterVisibleAreaOne(0.35f, 0.0f, 2.0f);
engine::Wrapper gCraterVisibleAreaTwo(0.35f, 0.0f, 2.0f);
engine::Wrapper gCraterVisibleAreaThree(0.25f, 0.03f, 1.5f);
engine::Wrapper gCraterVisibleAreaFour(0.0f, 0.0f, 1.5f);
engine::Wrapper gCraterVisibleIntensityOne(2.0f, 0.2f, 10.0f);
engine::Wrapper gCraterVisibleIntensityTwo(2.0f, 0.2f, 10.0f);
engine::Wrapper gCraterVisibleIntensityThree(1.0f, 0.1f, 5.0f);
engine::Wrapper gCraterVisibleIntensityFour(0.0f, 0.0f, 5.0f);

// Players - Area light
engine::Wrapper gPlayerAreaLightVisibleIntensity(1.0f, 0.1f, 1.0f);

// Players - Impact point light
engine::Wrapper gPlayerImpactVisibleAreaOne(0.675f, 0.07f, 3.5f);
engine::Wrapper gPlayerImpactVisibleAreaTwo(0.0f, 0.0f, 3.5f);
engine::Wrapper gPlayerImpactVisibleIntensityOne(1.0f, 0.1f, 5.0f);
engine::Wrapper gPlayerImpactVisibleIntensityTwo(0.5f, 0.0f, 2.5f);

// Players - Hex shield
engine::Wrapper gHexShieldIntensityDecay(1.25f, 0.1f, 6.0f);

// Missiles - Exhaust
engine::Wrapper gMissileExhaustVisibleIntensity(1.0f, 0.1f, 5.0f);

// Spaceships - Enemy blaster
engine::Wrapper gEnemyBlasterVisibleIntensity(1.0f, 0.1f, 5.0f);

// Spaceships - Hit flash
engine::Wrapper gHitFlashVisibleAreaOne(0.375f, 0.04f, 1.9f);
engine::Wrapper gHitFlashVisibleAreaTwo(0.0f, 0.0f, 1.9f);
engine::Wrapper gHitFlashVisibleIntensityOne(1.0f, 0.1f, 5.0f);
engine::Wrapper gHitFlashVisibleIntensityTwo(0.0f, 0.0f, 5.0f);

} // namespace game

#endif // defined(BT_CLIENT)
