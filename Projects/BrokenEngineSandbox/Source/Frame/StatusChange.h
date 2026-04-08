#pragma once

namespace game
{

enum class StatusChangeType : uint8_t
{
	kSpawnPlayer,
	kRespawnPlayer,
	kTransferPlayer,
	kTransferSpaceship,
	kTransferBlaster,
	kTransferMissile,
	kDestroyPlayer,
	kUpdatePlayer,
	kUpdateFlagshipCoord,
};

inline bool IsTransferType(StatusChangeType eType)
{
	return eType >= StatusChangeType::kTransferPlayer && eType <= StatusChangeType::kTransferMissile;
}

inline const char* StatusChangeTypeName(StatusChangeType eType)
{
	switch (eType)
	{
		case StatusChangeType::kSpawnPlayer:       return "SpawnPlayer";
		case StatusChangeType::kRespawnPlayer:     return "RespawnPlayer";
		case StatusChangeType::kTransferPlayer:    return "TransferPlayer";
		case StatusChangeType::kTransferSpaceship: return "TransferSpaceship";
		case StatusChangeType::kTransferBlaster:   return "TransferBlaster";
		case StatusChangeType::kTransferMissile:   return "TransferMissile";
		case StatusChangeType::kDestroyPlayer:     return "DestroyPlayer";
		case StatusChangeType::kUpdatePlayer:          return "UpdatePlayer";
		case StatusChangeType::kUpdateFlagshipCoord:  return "UpdateFlagshipCoord";
	}
	return "Unknown";
}

struct SpawnPlayerData
{
	int64_t iGlobalId = 0;
	bool bIsFlagship = false;
	engine::GridCoord flagshipCoord {};
	bool operator==(const SpawnPlayerData&) const = default;

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.iGlobalId, rSelf.bIsFlagship, rSelf.flagshipCoord);
	}
};

struct DestroyPlayerData
{
	int64_t iPlayerUuid = 0;
	bool operator==(const DestroyPlayerData&) const = default;
};

struct UpdatePlayerData
{
	int64_t iPlayerUuid = 0;
	bool bUseMissiles = false;
	float fNavigationDelay = 2.0f;
	bool operator==(const UpdatePlayerData&) const = default;

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.iPlayerUuid, rSelf.bUseMissiles, rSelf.fNavigationDelay);
	}
};

struct UpdateFlagshipCoordData
{
	int64_t iPlayerUuid = 0;
	bool bIsFlagship = false;
	engine::GridCoord flagshipCoord {};
	bool operator==(const UpdateFlagshipCoordData&) const = default;

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(rSelf.iPlayerUuid, rSelf.bIsFlagship, rSelf.flagshipCoord);
	}
};

struct TransferData
{
	bool operator==(const TransferData& rOther) const
	{
		bool bEqual = SharedMembers() == rOther.SharedMembers();
#if defined(BT_CLIENT)
		bEqual = bEqual && smokeTrailId == rOther.smokeTrailId;
#endif
		return bEqual;
	}

	auto SharedMembers(this auto&& rSelf)
	{
		return std::tie(
			rSelf.vecPosition, rSelf.vecDirection, rSelf.vecVelocity,
			rSelf.alignment,
			rSelf.fHealth, rSelf.fShield, rSelf.uiTypeIndex,
			rSelf.fWindTrailIntensity, rSelf.fWindTrailWidth, rSelf.fWindTrailLengthMultiplier,
			rSelf.fAcceleration,
			rSelf.fNextBlasterFireTime, rSelf.fNextSecondarySpawnTime, rSelf.fShieldCooldown, rSelf.fShieldDownSoundCooldown,
			rSelf.fAnimationTime, rSelf.fShieldRotation, rSelf.fShieldShrink, rSelf.uiPlayerFlags,
			rSelf.fNextBlasterSpawnTime,
			rSelf.fArrivalGracePeriod,
			rSelf.fNavigationDelay,
			rSelf.fDeltaRotationDelay, rSelf.fTime, rSelf.fExhaustDelay, rSelf.fNextJitter,
			rSelf.globalPlayerId,
			rSelf.flagshipCoord);
	}

	XMVECTOR vecPosition {};
	XMVECTOR vecDirection {};
	XMVECTOR vecVelocity {};
	engine::alignment_t alignment {};
	float fHealth = 0.0f;
	float fShield = 0.0f;
	uint8_t uiTypeIndex = 0;
	float fWindTrailIntensity = 0.0f;
	float fWindTrailWidth = 0.0f;
	float fWindTrailLengthMultiplier = 1.0f;
	float fAcceleration = 0.0f;

	// Player timers
	float fNextBlasterFireTime = 0.0f;
	float fNextSecondarySpawnTime = 0.0f;
	float fShieldCooldown = 0.0f;
	float fShieldDownSoundCooldown = 0.0f;

	// Player interpolate state
	float fAnimationTime = 0.0f;
	float fShieldRotation = 0.0f;
	float fShieldShrink = 1.0f;
	uint8_t uiPlayerFlags = 0;

	// Spaceship timers
	float fNextBlasterSpawnTime = 0.0f;

	// Arrival grace period (shared across player/spaceship transfers)
	float fArrivalGracePeriod = 0.0f;

	// Navigation delay (player transfers only)
	float fNavigationDelay = 2.0f;

	// Missile timers
	float fDeltaRotationDelay = 0.0f;
	float fTime = 0.0f;
	float fExhaustDelay = 0.0f;
	float fNextJitter = 0.0f;

	// Global player ID (player transfers only)
	engine::global_id_t globalPlayerId {};

	// Flagship coord (player transfers only)
	engine::GridCoord flagshipCoord {};

	// Client GUID (player transfers only, not serialized over network)
	uint64_t uiClientGuidHigh = 0;
	uint64_t uiClientGuidLow = 0;

	// Smoke trail ID reuse (missiles only)
#if defined(BT_CLIENT)
	engine::smoke_trails_t smokeTrailId {};
#endif
};

using StatusChangeData = std::variant<
	SpawnPlayerData,           // kSpawnPlayer
	TransferData,              // kTransfer* (all 4 types) and kRespawnPlayer (empty TransferData)
	DestroyPlayerData,         // kDestroyPlayer
	UpdatePlayerData,          // kUpdatePlayer
	UpdateFlagshipCoordData    // kUpdateFlagshipCoord
>;

inline StatusChangeData DefaultDataForType(StatusChangeType eType)
{
	switch (eType)
	{
		case StatusChangeType::kSpawnPlayer:      return SpawnPlayerData{};
		case StatusChangeType::kDestroyPlayer:    return DestroyPlayerData{};
		case StatusChangeType::kUpdatePlayer:          return UpdatePlayerData{};
		case StatusChangeType::kUpdateFlagshipCoord:  return UpdateFlagshipCoordData{};
		default:                                      return TransferData{};
	}
}

struct StatusChange
{
	bool operator==(const StatusChange&) const = default;

	StatusChangeType eType {};
	StatusChangeData data {};
};

} // namespace game
