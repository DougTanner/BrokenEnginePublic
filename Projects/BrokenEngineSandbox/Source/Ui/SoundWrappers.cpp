#if defined(BT_CLIENT)

#include "SoundWrappers.h"

namespace game
{

// Volume defaults are the tuned per-channel mix; adjust each one via the Sound Tweaks tab.
// Pitch defaults are real (pitch=0 would be silent-DC, not zero-volume).

// Blasters - Player
engine::Wrapper gPlayerBlasterVolume(0.1f, 0.0f, 0.2f);
engine::Wrapper gPlayerBlasterPitchMin(0.75f, 0.5f, 1.5f);
engine::Wrapper gPlayerBlasterPitchRandom(0.5f, 0.0f, 1.0f);

// Blasters - Enemy
engine::Wrapper gEnemyBlasterVolume(0.1f, 0.0f, 0.2f);
engine::Wrapper gEnemyBlasterPitchMin(1.5f, 0.5f, 2.0f);
engine::Wrapper gEnemyBlasterPitchRandom(1.0f, 0.0f, 1.5f);

// Blasters - Terrain impact
engine::Wrapper gTerrainImpactVolume(0.075f, 0.0f, 0.2f);

// Missiles
engine::Wrapper gMissileLaunchVolume(0.05f, 0.0f, 0.2f);
engine::Wrapper gMissileLoopVolume(0.04f, 0.0f, 0.2f);
engine::Wrapper gMissilePitchMin(1.0f, 0.5f, 1.5f);
engine::Wrapper gMissilePitchRandom(1.0f, 0.0f, 1.0f);

// Explosions
engine::Wrapper gExplosionVolume(0.075f, 0.0f, 0.2f);

// Players - Shield
engine::Wrapper gShieldHitVolumeBase(0.05f, 0.0f, 0.2f);
engine::Wrapper gShieldHitVolumeScale(0.0f, 0.0f, 0.2f);
engine::Wrapper gShieldDownVolume(0.0f, 0.0f, 0.2f);

// Players - Armor
engine::Wrapper gArmorHitVolumeBase(0.0f, 0.0f, 0.2f);
engine::Wrapper gArmorHitVolumeScale(0.0f, 0.0f, 0.2f);

// Spaceships
engine::Wrapper gSpaceshipDeathVolume(0.1f, 0.0f, 0.2f);
engine::Wrapper gSpaceshipDeathPitchMin(0.75f, 0.5f, 1.5f);
engine::Wrapper gSpaceshipDeathPitchRandom(0.5f, 0.0f, 1.0f);
engine::Wrapper gSpaceshipHitVolume(0.075f, 0.0f, 0.2f);

} // namespace game

#endif // defined(BT_CLIENT)
