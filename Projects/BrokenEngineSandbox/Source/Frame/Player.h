#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/HexShields.h"
#include "Frame/Collections/WindTrails.h"
#include "Frame/HealthDamage.h"

namespace game
{

struct HexShieldDirections
{
	XMFLOAT4 data[shaders::kiHexShieldDirections] {};
	bool operator==(const HexShieldDirections& rOther) const { return std::memcmp(data, rOther.data, sizeof(data)) == 0; }
};

struct HexShieldIntensities
{
	float data[shaders::kiHexShieldDirections] {};
	bool operator==(const HexShieldIntensities& rOther) const { return std::memcmp(data, rOther.data, sizeof(data)) == 0; }
};

struct PlayersInterpolate : public engine::Collection<PlayersInterpolate, engine::CollectionFlags::kIdToIndex>
{
	static constexpr int64_t kiVersion = 10;
	static constexpr char kName[] = "Player";
	static constexpr common::crc_t kCrc = common::CrcConsteval(kName);

	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	static inline uint8_t suiAreaLightTypeIndex = 0xFF;
	static inline uint8_t suiBlasterTypeIndex = 0xFF;
	static inline uint8_t suiExplosionTypeIndex = 0xFF;
	static inline uint8_t suiImpactPuffControllerTypeIndex = 0xFF;
	static inline uint8_t suiImpactPointLightControllerTypeIndex = 0xFF;
	static inline uint8_t suiHexShieldTypeIndex = 0xFF;

	// Allocate and copy
	static void AllocateAndCopy(PlayersInterpolate& rCurrent, const PlayersInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rFrameInterpolate, const Frame& __restrict rPreviousFrame);

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	engine::wind_trail_t* __restrict pWindTrails = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	float* __restrict pfAnimationTimes = nullptr;
	float* __restrict pfRotationAccelerationXs = nullptr;
	float* __restrict pfRotationAccelerationYs = nullptr;
	engine::hex_shields_t* __restrict pHexShields = nullptr;
	float* __restrict pfShieldRotations = nullptr;
	float* __restrict pfShieldShrinks = nullptr;
	HexShieldDirections* __restrict pHexShieldDirections = nullptr;
	HexShieldIntensities* __restrict pHexShieldVertIntensities = nullptr;
	HexShieldIntensities* __restrict pHexShieldFragIntensities = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.pWindTrails,
			rSelf.pfDestroyedTimes, rSelf.pfAnimationTimes,
			rSelf.pfRotationAccelerationXs, rSelf.pfRotationAccelerationYs,
			rSelf.pHexShields, rSelf.pfShieldRotations, rSelf.pfShieldShrinks,
			rSelf.pHexShieldDirections, rSelf.pHexShieldVertIntensities, rSelf.pHexShieldFragIntensities);
	}

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const PlayersInterpolate& rOther) const;
};

using player_t = PlayersInterpolate::id_t;

enum class PlayerFlags : uint8_t
{
	kExploding        = 0x01,
	kFireBlaster      = 0x02,
	kBlasterSpawnLeft = 0x04,
	kFireMissile      = 0x08,
	kMissileSpawnLeft = 0x10,
	kTransfer         = 0x20,
};
using PlayerFlags_t = common::Flags<PlayerFlags>;

struct PlayersPostRender : public engine::Collection<PlayersPostRender>
{
	static constexpr int64_t kiVersion = 7;

	// Collision layer (set each frame in PreCollision)
	static inline int64_t siCollisionLayerIndex = 0;

	// Allocate and copy
	static void AllocateAndCopy(PlayersPostRender& rCurrent, const PlayersPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Transfer(Frame& __restrict rFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput);

	player_t* __restrict puiIds = nullptr;
	PlayerFlags_t* __restrict pFlags = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	float* __restrict pfNextBlasterFireTimes = nullptr;
	float* __restrict pfNextSecondarySpawnTimes = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecWantedDirections = nullptr;
	float* __restrict pfArmors = nullptr;
	float* __restrict pfShields = nullptr;
	float* __restrict pfShieldCooldowns = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfShieldDownSoundCooldowns = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.pAlignments,
			rSelf.pfNextBlasterFireTimes, rSelf.pfNextSecondarySpawnTimes,
			rSelf.pVecVelocities, rSelf.pVecWantedDirections,
			rSelf.pfArmors, rSelf.pfShields, rSelf.pfShieldCooldowns,
			rSelf.pfDestroyedExplosionTimes, rSelf.pfShieldDownSoundCooldowns);
	}

	// Utility
	bool operator==(const PlayersPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
		XMVECTOR vecVelocity {};
		engine::alignment_t alignment {};
		float fArmor = 0.0f;
		float fShield = 0.0f;
		float fNextBlasterFireTime = 0.0f;
		float fNextSecondarySpawnTime = 0.0f;
		float fShieldCooldown = 0.0f;
		float fShieldDownSoundCooldown = 0.0f;
		float fAnimationTime = 0.0f;
		float fShieldRotation = 0.0f;
		float fShieldShrink = 1.0f;
		PlayerFlags_t flags = {PlayerFlags::kBlasterSpawnLeft};
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game
