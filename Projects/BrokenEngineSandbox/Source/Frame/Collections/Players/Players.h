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
inline constexpr float kfPlayerBlastersSpeed = 150.0f;

// Collision
inline constexpr float kfPlayerRadius = 1.1f;

// Terrain push
inline constexpr float kfPushMargin = kfPlayerRadius * 0.6667f;

// Movement — three independent tuning axes
inline constexpr float kfPlayerAcceleration = 75.0f;
inline constexpr float kfPlayerCatchUpAcceleration = 100.0f;
inline constexpr float kfPlayerDrag = 1.0f;
inline constexpr float kfPlayerMaxSpeed = 33.33f;
inline constexpr float kfPlayerCatchUpMaxSpeed = 33.33f;
inline constexpr float kfPlayerJitterRange = 0.60f;

// Pusher
inline constexpr float kfPlayerPusherRadius = kfPlayerRadius * 3.3333f;
inline constexpr float kfPlayerPusherIntensity = 50.0f;
inline constexpr float kfPlayerPusherPower = 2.0f;
inline constexpr float kfPlayerMaxPusherPushVelocity = kfPlayerMaxSpeed * 0.5f;

#if defined(BT_CLIENT)
struct HexShieldDirections
{
	XMFLOAT4A data[shaders::kiHexShieldDirections] {};
};

struct HexShieldIntensities
{
	float data[shaders::kiHexShieldDirections] {};
};
#endif // BT_CLIENT

struct PlayersInterpolate : public engine::Collection<PlayersInterpolate, engine::CollectionFlags::kIdToIndex>
{
	static constexpr int64_t kiVersion = 13;
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
	engine::pusher_t* __restrict puiPushers = nullptr;
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
#endif // BT_CLIENT

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections,
			rSelf.pfDestroyedTimes, rSelf.pfAnimationTimes, rSelf.puiPushers);
	}
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pfRotationAccelerationXs, rSelf.pfRotationAccelerationYs,
			rSelf.pWindTrails,
			rSelf.pHexShields, rSelf.pfShieldRotations, rSelf.pfShieldShrinks,
			rSelf.pHexShieldDirections, rSelf.pHexShieldVertIntensities, rSelf.pHexShieldFragIntensities);
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

	// CRC-only subset: excludes pfAnimationTimes (client-only update) and puiPushers (local collection index)
	auto SharedCrcMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecPositions, rSelf.pVecDirections,
			rSelf.pfDestroyedTimes);
	}

	// Render
	static void BeginRender(int64_t iCommandBuffer, const std::unordered_map<engine::GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<engine::GridCoord>& rActiveCoords);
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
	static void DebugRender(const FrameInterpolate& __restrict rFrameInterpolate, engine::GridCoord coord);

	// Utility
	bool LogDifferences(const PlayersInterpolate& rOther) const;

#if defined(BT_CLIENT)
	// Helper shared by Players.cpp (Destroy, spawn-status-change cleanup) and PlayersNavigation.cpp (Transfer)
	static void RemoveOwnedVisuals(Frame& rFrame, PlayersInterpolate& rCurrentInterpolate, int64_t i);
#endif
};

enum class PlayerFlags : uint16_t
{
	kExploding        = 0x0001,
	kFireBlaster      = 0x0002,
	kBlasterSpawnLeft = 0x0004,
	kFireMissile      = 0x0008,
	kMissileSpawnLeft = 0x0010,
	kTransfer         = 0x0020,
	kUseMissiles      = 0x0040,
	kIsFlagship       = 0x0080,

	// Nav direction in bits 8-10 (3 bits, stored as value+1: 0-6 representing -1 to 5)
	kNavDirectionBit0 = 0x0100,
	kNavDirectionBit1 = 0x0200,
	kNavDirectionBit2 = 0x0400,

	// Pending weapon mode (bit 11)
	kPendingUseMissiles = 0x0800,

	// Nav waypoint index in bits 12-13 (2 bits): 0 = largest island, 1 = smallest, 2 = random (saturates at 2)
	kNavWaypointBit0 = 0x1000,
	kNavWaypointBit1 = 0x2000,
};
using PlayerFlags_t = common::Flags<PlayerFlags>;

inline constexpr int8_t GetNavDirection(PlayerFlags_t flags)
{
	return static_cast<int8_t>(((std::to_underlying(flags.meFlags) >> 8) & 0x7) - 1);
}

inline void SetNavDirection(PlayerFlags_t& rFlags, int8_t iDirection)
{
	uint16_t uiRaw = std::to_underlying(rFlags.meFlags);
	uiRaw = static_cast<uint16_t>((uiRaw & ~0x0700) | (static_cast<uint16_t>(iDirection + 1) << 8));
	rFlags.meFlags = static_cast<PlayerFlags>(uiRaw);
}

inline constexpr int8_t GetNavWaypointIndex(PlayerFlags_t flags)
{
	return static_cast<int8_t>((std::to_underlying(flags.meFlags) >> 12) & 0x3);
}

inline void SetNavWaypointIndex(PlayerFlags_t& rFlags, int8_t iIndex)
{
	uint16_t uiRaw = std::to_underlying(rFlags.meFlags);
	uiRaw = static_cast<uint16_t>((uiRaw & ~0x3000) | (static_cast<uint16_t>(iIndex) << 12));
	rFlags.meFlags = static_cast<PlayerFlags>(uiRaw);
}

struct PlayersPostRender : public engine::Collection<PlayersPostRender>
{
	static constexpr int64_t kiVersion = 19;

	// Collision layer (set each frame in PreCollision)
	// thread_local: parallel per-Frame tick via Dispatch
	static thread_local int64_t siCollisionLayerIndex;

	// Allocate and copy
	static void AllocateAndCopy(PlayersPostRender& rCurrent, const PlayersPostRender& rPrevious);

	// Update (orchestrator in Players.cpp; per-player phase work split into helpers below)
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);

	// Collision (PreCollision: Players.cpp; PostCollision/AreaDamage: PlayersCombat.cpp)
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData);

	// Lifecycle (Transfer: PlayersNavigation.cpp; Destroy/Spawn: Players.cpp)
	static void Transfer(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Destroy(Frame& __restrict rFrame, const engine::FrameStaticData& rStaticData);
	static void Spawn(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, const engine::FrameStaticData& rStaticData);

private:
	// Per-player Update helpers (called from PlayersPostRender::Update orchestrator)
	// Defined in PlayersNavigation.cpp:
	static void XM_CALLCONV ComputeNavigation(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData, int64_t i, FXMVECTOR vecPosition, FXMVECTOR vecFrameCenter, engine::GridCoord fleetWantedCoord, uint8_t uiPendingFleetWantedCoordTicks, PlayerFlags_t flags, float fDeltaTime, int8_t& riNavDirection, int8_t& riNavWaypointIndex, XMVECTOR& rVecAiDirection, XMVECTOR& rVecIslandDestination, float& rfFrameChangeTimer);
	static void XM_CALLCONV ApplyMovement(int8_t iNavDirection, FXMVECTOR vecAiDirection, float fDeltaTime, float fAccelMul, float fDecayMul, XMVECTOR& rVecVelocity);
	static void XM_CALLCONV ApplyTerrainPush(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, XMVECTOR& rVecVelocity);
	static void XM_CALLCONV ApplyPusherPush(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, int64_t i, FXMVECTOR vecPosition, XMVECTOR& rVecVelocity);

	// Defined in PlayersCombat.cpp:
	static void XM_CALLCONV AcquireTarget(const Frame& __restrict rPreviousFrame, FXMVECTOR vecPosition, PlayerFlags_t& rFlags, bool& rbLookTargetFound, XMVECTOR& rVecLookPosition);
	static void XM_CALLCONV UpdateFacing(FXMVECTOR vecPosition, FXMVECTOR vecLookPosition, XMVECTOR& rVecWantedDirection);
	static void RegenerateShield(float fDeltaTime, float fShieldCooldown, float& rfShield);

	// Weapon and death-explosion spawn helpers (called from PlayersPostRender::Spawn). Defined in PlayersCombat.cpp.
	static void SpawnBlasters(Frame& __restrict rFrame);
	static void SpawnMissiles(Frame& __restrict rFrame);
	static void SpawnDeathExplosions(Frame& __restrict rFrame);

public:

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
	XMVECTOR* __restrict pVecIslandDestinations = nullptr;
	engine::ClientGuid* __restrict pClientGuids = nullptr;
	engine::global_id_t* __restrict pGlobalPlayerIds = nullptr;
	float* __restrict pfNavigationDelays = nullptr;
	engine::GridCoord* __restrict pFleetWantedCoords = nullptr;
	uint8_t* __restrict puiPendingFleetWantedCoordTicks = nullptr;
	uint8_t* __restrict puiPendingWeaponModeTicks = nullptr;
#if defined(BT_CLIENT)
	XMVECTOR* __restrict pVecDebugNavWaypoints = nullptr;
#endif // BT_CLIENT

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.puiIds, rSelf.pFlags, rSelf.pAlignments,
			rSelf.pfNextBlasterFireTimes, rSelf.pfNextSecondarySpawnTimes,
			rSelf.pVecVelocities, rSelf.pVecWantedDirections,
			rSelf.pfArmors, rSelf.pfShields, rSelf.pfShieldCooldowns,
			rSelf.pfDestroyedExplosionTimes, rSelf.pfShieldDownSoundCooldowns,
			rSelf.pVecAiDirections,
			rSelf.pfTransferLockTimers, rSelf.pfArrivalGracePeriods,
			rSelf.pfFrameChangeTimers,
			rSelf.pVecIslandDestinations,
			rSelf.pClientGuids, rSelf.pGlobalPlayerIds,
			rSelf.pfNavigationDelays,
			rSelf.pFleetWantedCoords,
			rSelf.puiPendingFleetWantedCoordTicks,
			rSelf.puiPendingWeaponModeTicks);
	}
#if defined(BT_CLIENT)
	auto ClientMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.pVecDebugNavWaypoints);
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
			rSelf.pfFrameChangeTimers,
			rSelf.pVecIslandDestinations,
			rSelf.pfNavigationDelays,
			rSelf.pFleetWantedCoords,
			rSelf.puiPendingFleetWantedCoordTicks,
			rSelf.puiPendingWeaponModeTicks);
	}

	// Utility
	bool LogDifferences(const PlayersPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition = DirectX::XMVectorZero();
		XMVECTOR vecDirection = DirectX::XMVectorZero();
		XMVECTOR vecVelocity = DirectX::XMVectorZero();
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
		float fNavigationDelay = 60.0f;
		engine::global_id_t globalPlayerId {};
		engine::GridCoord fleetWantedCoord {};
		uint8_t uiPendingFleetWantedCoordTicks = 0;
		uint8_t uiPendingWeaponModeTicks = 0;
		// Arrival from a neighbouring cell: restore every carried value verbatim instead of defaulting.
		bool bTransfer = false;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game

namespace engine
{
extern template struct Collection<game::PlayersInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<game::PlayersPostRender>;
}
