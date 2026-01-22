#pragma once

#include "Frame/CollisionGroups.h"

namespace game
{

// Collision group IDs for alignment-based filtering
// Initialized when a new game starts, restored when loading a game
// Invalid group (kInvalidCollisionGroup) collides with everything
inline engine::collision_group_t gPlayerGroup {};  // Player and player-owned objects
inline engine::collision_group_t gEnemyGroup {};   // Enemies and enemy-owned objects

// Initialize collision groups for a new game
// Creates player/enemy groups and sets up matrix so same-alignment doesn't collide
void InitializeCollisionGroups(engine::FramePostRenderBase& rPostRender);

// Restore collision group globals from loaded frame state
void RestoreCollisionGroupGlobals(const engine::collision_group_t& rPlayerGroup, const engine::collision_group_t& rEnemyGroup);

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

inline constexpr float kfPlayerMissileCapacity = 10.0f;

inline constexpr float kfSpaceshipArmorShardChance = 0.1f;

// Enemy health
inline constexpr float kfBlasterDamage = 6.0f;
inline constexpr float kfMissileCollisionRadius = 0.5f;
inline constexpr float kfMissileDamageRadius = 7.0f;
inline constexpr float kfMissileDamage = 30.0f;

inline constexpr float kfSpaceshipHealth = 10.0f;
inline constexpr float kfSpaceshipCollisionDamage = 5.0f;

// Collision Categories - What type am I?
namespace CollisionCategory
{
	inline constexpr uint16_t kNone            = 0x0000;
	inline constexpr uint16_t kBlasterPlayer   = 0x0001; // Fired by player, hits spaceships
	inline constexpr uint16_t kBlasterSpaceship = 0x0002; // Fired by spaceships, hits player
	inline constexpr uint16_t kSpaceship       = 0x0004;
	inline constexpr uint16_t kPlayer          = 0x0008;
	inline constexpr uint16_t kMissile         = 0x0010; // Fired by player, hits spaceships only
}

// Collision Masks - What types can I collide with?
namespace CollisionMask
{
	inline constexpr uint16_t kNone = 0x0000;

	// Player blasters hit spaceships only
	inline constexpr uint16_t kBlasterPlayer = CollisionCategory::kSpaceship;

	// Spaceship blasters hit player only
	inline constexpr uint16_t kBlasterSpaceship = CollisionCategory::kPlayer;

	// Missiles hit spaceships only (never player)
	inline constexpr uint16_t kMissile = CollisionCategory::kSpaceship;

	// Spaceships collide with player, player blasters, and missiles
	inline constexpr uint16_t kSpaceship = CollisionCategory::kPlayer | CollisionCategory::kBlasterPlayer | CollisionCategory::kMissile;

	// Player collides with spaceships and spaceship blasters (not missiles)
	inline constexpr uint16_t kPlayer = CollisionCategory::kSpaceship | CollisionCategory::kBlasterSpaceship;
}

} // namespace game
