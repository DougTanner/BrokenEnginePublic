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
inline constexpr float kfSpaceshipCollisionDamage = 5.0f;

// Collision Categories - What type am I?
namespace CollisionCategory
{
	inline constexpr uint16_t kNone      = 0x0000;
	inline constexpr uint16_t kBlaster   = 0x0001;
	inline constexpr uint16_t kSpaceship = 0x0002;
	inline constexpr uint16_t kPlayer    = 0x0004;
}

// Collision Masks - What types can I collide with?
namespace CollisionMask
{
	inline constexpr uint16_t kNone = 0x0000;

	// Player blasters hit spaceships only
	inline constexpr uint16_t kPlayerBlaster = CollisionCategory::kSpaceship;

	// Spaceships collide with player and player blasters
	inline constexpr uint16_t kSpaceship = CollisionCategory::kPlayer | CollisionCategory::kBlaster;

	// Player collides with spaceships only
	inline constexpr uint16_t kPlayer = CollisionCategory::kSpaceship;
}

} // namespace game
