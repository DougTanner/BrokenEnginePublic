#pragma once

namespace game
{

// Player health
enum Damages
{
	kDamageSpaceshipBlaster,
	kDamageSpaceshipCollision,

	kDamagesCount,
};

inline constexpr float kppfDamages[kDamagesCount][3] =
{
	{2.0f, 3.0f, 4.0f}, // kDamageSpaceshipBlaster
	{5.0f, 10.0f, 15.0f}, // kDamageSpaceshipCollision
};

inline constexpr float kfPlayerArmor = 25.0f;
inline constexpr float kfPlayerShield = 50.0f;
inline constexpr float kfPlayerShieldRegen = 5.0f;
inline constexpr float kfPlayerShieldPenetration = 0.05f;

inline constexpr float kfPlayerEnergy = 25.0f;

inline constexpr float kfPlayerMissileCapacity = 2.0f;

inline constexpr float kfSpaceshipArmorShardChance = 0.1f;

// Enemy health
inline constexpr float kfBlasterDamage = 1.65f;
inline constexpr float kfMissileCollisionRadius = 2.0f;
inline constexpr float kfMissileDamageRadius = 3.0f;
inline constexpr float kfMissileDamage = 22.5f;

inline constexpr float kfSpaceshipHealth = 10.0f;

} // namespace game
