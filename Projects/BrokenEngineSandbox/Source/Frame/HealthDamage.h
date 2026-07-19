#pragma once

#include "Frame/Alignments.h"

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

inline constexpr float kfPlayerArmor = 50.0f;
inline constexpr float kfPlayerShield = 100.0f;
inline constexpr float kfPlayerShieldRegen = 5.0f;

inline constexpr float kfPlayerEnergy = 25.0f;

inline constexpr float kfSpaceshipArmorShardChance = 0.1f;

// Arrival grace period: arriving entities are skipped in targeting/behavior scans (not collision)
inline constexpr float kfArrivalGracePeriod = 1.0f;

// Enemy health
inline constexpr float kfBlasterDamage = 6.0f;
inline constexpr float kfMissileCollisionRadius = 0.5f;
inline constexpr float kfMissileDamageRadius = 7.0f;
inline constexpr float kfMissileDamage = 30.0f;
inline constexpr float kfMissileLifetime = 10.0f;
inline constexpr float kfMissileGravity = 9.8f;

inline constexpr float kfSpaceshipHealth = 10.0f;
inline constexpr float kfSpaceshipCollisionDamage = 5.0f;

// What type am I?
namespace CollisionCategory
{
	inline constexpr uint16_t kNone      = 0x0000;
	inline constexpr uint16_t kBlaster   = 0x0001;
	inline constexpr uint16_t kSpaceship = 0x0004;
	inline constexpr uint16_t kPlayer    = 0x0008;
	inline constexpr uint16_t kMissile   = 0x0010;
}

// What types can I collide with?
namespace CollidesWith
{
	inline constexpr uint16_t kNone = CollisionCategory::kNone;

	// Blasters hit both spaceships and player (alignment filters same-team)
	inline constexpr uint16_t kBlaster = CollisionCategory::kSpaceship | CollisionCategory::kPlayer;

	// Missiles hit spaceships only (never player)
	inline constexpr uint16_t kMissile = CollisionCategory::kSpaceship;

	// Spaceships collide with player, blasters, and missiles
	inline constexpr uint16_t kSpaceship = CollisionCategory::kPlayer | CollisionCategory::kBlaster | CollisionCategory::kMissile;

	// Player collides with spaceships and blasters
	inline constexpr uint16_t kPlayer = CollisionCategory::kSpaceship | CollisionCategory::kBlaster;
}

} // namespace game
