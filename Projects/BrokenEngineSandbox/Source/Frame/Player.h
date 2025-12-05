#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/HexShields.h"
#include "Frame/HealthDamage.h"

namespace game
{

struct PlayerInterpolate
{
	static constexpr int64_t kiVersion = 5;
	static constexpr char kpcName[] = "Player";
	static constexpr common::crc_t kCrc = common::Crc(kpcName);

	// Register
	static void Register();

	static inline uint8_t suiAreaLightTypeIndex = 0xFF;
	static inline uint8_t suiBlasterTypeIndex = 0xFF;
	static inline uint8_t suiExplosionTypeIndex = 0xFF;
	static inline uint8_t suiImpactPuffControllerTypeIndex = 0xFF;
	static inline uint8_t suiImpactPointLightControllerTypeIndex = 0xFF;

	// Interpolate
	static void Update(PlayerInterpolate& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Sync(FrameInterpolate& __restrict rFrameInterpolate, const FramePostRender& __restrict rFramePostRender);

	XMVECTOR vecPosition {0.0f, 0.0f, 0.0f, 1.0f};
	XMVECTOR vecDirection {1.0f, 0.0f, 0.0f, 0.0f};
	float fDestroyedTime = 0.0f;

	// Hex shield visual state
	engine::hex_shields_t uiHexShield {};
	float fShieldRotation = 0.0f;
	float fShieldShrink = 1.0f;
	XMFLOAT4 pf4HexShieldDirections[shaders::kiHexShieldDirections] {};
	float pfHexShieldVertIntensities[shaders::kiHexShieldDirections] {};
	float pfHexShieldFragIntensities[shaders::kiHexShieldDirections] {};

	// Render
	static void AllocatePipelines();
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const PlayerInterpolate& rOther) const;
	static common::crc_t Crc(const PlayerInterpolate& rCurrent);
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

enum class PlayerFlags : uint8_t
{
	kExploding        = 0x01,
	kFireBlaster      = 0x02,
	kBlasterSpawnLeft = 0x04,
	kFireMissile      = 0x08,
};
using PlayerFlags_t = common::Flags<PlayerFlags>;

struct PlayerPostRender
{
	static constexpr int64_t kiVersion = 4;

	// Collision layer (set each frame in PreCollision)
	static inline int64_t siCollisionLayerIndex = 0;

	// Update
	static void Update(PlayerPostRender& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime, const FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	PlayerFlags_t flags {PlayerFlags::kBlasterSpawnLeft};
	float fNextBlasterFireTime = 0.0f;
	float fNextSecondarySpawnTime = 0.0f;
	float fMissiles = kfPlayerMissileCapacity;
	XMVECTOR vecVelocity {0.0f, 0.0f, 0.0f, 0.0f};
	XMVECTOR vecWantedDirection {1.0f, 0.0f, 0.0f, 0.0f};
	float fArmor = kfPlayerArmor;
	float fShield = kfPlayerShield;
	float fShieldCooldown = 0.0f;
	float fDestroyedExplosionTime = 0.0f;
	float fShieldDownSoundCooldown = 0.0f;

	// Utility
	bool operator==(const PlayerPostRender& rOther) const;
	static common::crc_t Crc(const PlayerPostRender& rCurrent);
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

} // namespace game