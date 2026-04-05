#pragma once

#include "Frame/Alignments.h"
#include "Frame/GridCoord.h"
#include "Frame/Collections/Collection.h"

namespace engine { struct FrameStaticData; }
#if defined(BT_CLIENT)
#include "Frame/Collections/HexShields/HexShields.h"
#include "Frame/Collections/WindTrails/WindTrails.h"
#endif

#include "Frame/HealthDamage.h"

namespace game
{

// Shared constants (used across Players*.cpp files)
inline constexpr float kfDestroyTime = 0.7f;
inline constexpr float kfDestroyExplosionInterval = 0.005f;

// Collision
inline constexpr float kfPlayerRadius = 1.5f;

// Damage response
inline constexpr float kfShieldHitSoundVolumeBase = 0.1f;
inline constexpr float kfShieldHitSoundVolumeScale = 0.1f;
inline constexpr float kfShieldCooldown = 2.0f;
inline constexpr float kfShieldDownSoundCooldown = 2.0f;
inline constexpr float kfShieldDownSoundVolume = 0.1f;
inline constexpr float kfArmorHitSoundDamageThreshold = 3.0f;
inline constexpr float kfArmorHitSoundVolumeBase = 0.2f;
inline constexpr float kfArmorHitSoundVolumeScale = 0.5f;

#if defined(BT_CLIENT)
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
#endif // BT_CLIENT

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
	float* __restrict pfDestroyedTimes = nullptr;
	float* __restrict pfAnimationTimes = nullptr;
#if defined(BT_CLIENT)
	float* __restrict pfRotationAccelerationXs = nullptr;
	float* __restrict pfRotationAccelerationYs = nullptr;
	engine::wind_trail_t* __restrict pWindTrails = nullptr;
	engine::hex_shields_t* __restrict pHexShields = nullptr;
	float* __restrict pfShieldRotations = nullptr;
	float* __restrict pfShieldShrinks = nullptr;
	HexShieldDirections* __restrict pHexShieldDirections = nullptr;
	HexShieldIntensities* __restrict pHexShieldVertIntensities = nullptr;
	HexShieldIntensities* __restrict pHexShieldFragIntensities = nullptr;
	XMVECTOR* __restrict pVecDebugNavDestinations = nullptr;
#endif // BT_CLIENT

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections,
			rSelf.pfDestroyedTimes, rSelf.pfAnimationTimes);
	}
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pfRotationAccelerationXs, rSelf.pfRotationAccelerationYs,
			rSelf.pWindTrails,
			rSelf.pHexShields, rSelf.pfShieldRotations, rSelf.pfShieldShrinks,
			rSelf.pHexShieldDirections, rSelf.pHexShieldVertIntensities, rSelf.pHexShieldFragIntensities,
			rSelf.pVecDebugNavDestinations);
	}
#endif // BT_CLIENT
	auto Members(this auto&& rSelf)
	{
#if defined(BT_CLIENT)
		return std::tuple_cat(rSelf.SharedMembers(), rSelf.ClientMembers());
#else
		return rSelf.SharedMembers();
#endif
	}

	// CRC-only subset: excludes pfAnimationTimes which is only meaningfully updated on client
	auto SharedCrcMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections,
			rSelf.pfDestroyedTimes);
	}

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);

	// Utility
	bool LogDifferences(const PlayersInterpolate& rOther) const;
};

enum class PlayerFlags : uint8_t
{
	kExploding        = 0x01,
	kFireBlaster      = 0x02,
	kBlasterSpawnLeft = 0x04,
	kFireMissile      = 0x08,
	kMissileSpawnLeft = 0x10,
	kTransfer         = 0x20,
	kUseMissiles      = 0x40,
};
using PlayerFlags_t = common::Flags<PlayerFlags>;

struct PlayersPostRender : public engine::Collection<PlayersPostRender>
{
	static constexpr int64_t kiVersion = 12;

	// Collision layer (set each frame in PreCollision)
	// thread_local: parallel per-Frame tick via Dispatch
	static thread_local int64_t siCollisionLayerIndex;

	// Allocate and copy
	static void AllocateAndCopy(PlayersPostRender& rCurrent, const PlayersPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Spawn(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, const engine::FrameStaticData& rStaticData);

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
	XMVECTOR* __restrict pVecAiDirections = nullptr;
	float* __restrict pfTransferLockTimers = nullptr;
	float* __restrict pfArrivalGracePeriods = nullptr;
	float* __restrict pfFrameChangeTimers = nullptr;
	int8_t* __restrict piNavDirections = nullptr;
	engine::ClientGuid* __restrict pClientGuids = nullptr;
	engine::global_player_t* __restrict pGlobalPlayerIds = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.pAlignments,
			rSelf.pfNextBlasterFireTimes, rSelf.pfNextSecondarySpawnTimes,
			rSelf.pVecVelocities, rSelf.pVecWantedDirections,
			rSelf.pfArmors, rSelf.pfShields, rSelf.pfShieldCooldowns,
			rSelf.pfDestroyedExplosionTimes, rSelf.pfShieldDownSoundCooldowns,
			rSelf.pVecAiDirections,
			rSelf.pfTransferLockTimers, rSelf.pfArrivalGracePeriods,
			rSelf.pfFrameChangeTimers, rSelf.piNavDirections,
			rSelf.pClientGuids, rSelf.pGlobalPlayerIds);
	}

	// CRC-only subset: excludes pClientGuids and pGlobalPlayerIds which are server-side bookkeeping
	auto SharedCrcMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.pAlignments,
			rSelf.pfNextBlasterFireTimes, rSelf.pfNextSecondarySpawnTimes,
			rSelf.pVecVelocities, rSelf.pVecWantedDirections,
			rSelf.pfArmors, rSelf.pfShields, rSelf.pfShieldCooldowns,
			rSelf.pfDestroyedExplosionTimes, rSelf.pfShieldDownSoundCooldowns,
			rSelf.pVecAiDirections,
			rSelf.pfTransferLockTimers, rSelf.pfArrivalGracePeriods,
			rSelf.pfFrameChangeTimers, rSelf.piNavDirections);
	}

	// Utility
	bool LogDifferences(const PlayersPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition {};
		XMVECTOR vecDirection {};
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
		float fTransferLockTimer = 0.0f;
		float fArrivalGracePeriod = 0.0f;
		float fFrameChangeTimer = 0.0f;
		int8_t iNavDirection = -1;
		engine::global_player_t globalPlayerId {};
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::PlayersInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<game::PlayersPostRender>;
}
