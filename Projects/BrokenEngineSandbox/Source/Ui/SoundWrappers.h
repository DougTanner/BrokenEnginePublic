#pragma once

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace game
{

// Blasters - Player
extern engine::Wrapper gPlayerBlasterVolume;
extern engine::Wrapper gPlayerBlasterPitchMin;
extern engine::Wrapper gPlayerBlasterPitchRandom;

// Blasters - Enemy
extern engine::Wrapper gEnemyBlasterVolume;
extern engine::Wrapper gEnemyBlasterPitchMin;
extern engine::Wrapper gEnemyBlasterPitchRandom;

// Blasters - Terrain impact
extern engine::Wrapper gTerrainImpactVolume;

// Missiles
extern engine::Wrapper gMissileLaunchVolume;
extern engine::Wrapper gMissileLoopVolume;
extern engine::Wrapper gMissilePitchMin;
extern engine::Wrapper gMissilePitchRandom;

// Explosions
extern engine::Wrapper gExplosionVolume;

// Players - Shield
extern engine::Wrapper gShieldHitVolumeBase;
extern engine::Wrapper gShieldHitVolumeScale;
extern engine::Wrapper gShieldDownVolume;

// Players - Armor
extern engine::Wrapper gArmorHitVolumeBase;
extern engine::Wrapper gArmorHitVolumeScale;

// Spaceships
extern engine::Wrapper gSpaceshipDeathVolume;
extern engine::Wrapper gSpaceshipDeathPitchMin;
extern engine::Wrapper gSpaceshipDeathPitchRandom;
extern engine::Wrapper gSpaceshipHitVolume;

} // namespace game

#endif // defined(BT_CLIENT)
