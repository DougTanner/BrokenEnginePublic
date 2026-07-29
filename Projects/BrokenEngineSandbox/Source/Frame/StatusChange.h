#pragma once

namespace game
{

// Underlying values are serialized wire/save bytes — enumerators are append-only; never reorder or insert.
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
	kUpdateFleet,

	kCount,
};

// Trust-boundary predicate: a deserialized type tag is a valid enumerator only in [0, kCount). The replay
// FrameInput reader uses it to reject an unknown tag before it seats the wrong variant alternative.
inline constexpr bool IsKnownStatusChangeType(StatusChangeType eType)
{
	return eType < StatusChangeType::kCount;
}

inline constexpr bool IsTransferType(StatusChangeType eType)
{
	return eType >= StatusChangeType::kTransferPlayer && eType <= StatusChangeType::kTransferMissile;
}

static_assert(static_cast<uint8_t>(StatusChangeType::kTransferSpaceship) == static_cast<uint8_t>(StatusChangeType::kTransferPlayer) + 1 && static_cast<uint8_t>(StatusChangeType::kTransferBlaster) == static_cast<uint8_t>(StatusChangeType::kTransferPlayer) + 2 && static_cast<uint8_t>(StatusChangeType::kTransferMissile) == static_cast<uint8_t>(StatusChangeType::kTransferPlayer) + 3, "IsTransferType assumes kTransferPlayer..kTransferMissile stay contiguous; inserting an enumerator between them breaks the range check.");

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
		case StatusChangeType::kUpdatePlayer:      return "UpdatePlayer";
		case StatusChangeType::kUpdateFleet:       return "UpdateFleet";
		case StatusChangeType::kCount:             break;
	}
	return "Unknown";
}

struct SpawnPlayerData
{
	int64_t iGlobalId = 0;
	bool bIsFlagship = false;
	engine::GridCoord fleetWantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
	bool operator==(const SpawnPlayerData&) const = default;
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
	float fNavigationDelay = 60.0f;
	uint8_t uiPendingWeaponModeTicks = 0;
	bool operator==(const UpdatePlayerData&) const = default;
};

struct UpdateFleetData
{
	int64_t iPlayerUuid = 0;
	bool bIsFlagship = false;
	engine::GridCoord fleetWantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
	bool operator==(const UpdateFleetData&) const = default;
};

struct TransferData
{
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
			rSelf.fNavigationDelay,
			rSelf.fDeltaRotationDelay, rSelf.fTime, rSelf.fNextJitter,
			rSelf.fDeltaRotation, rSelf.fDeltaRotationMax, rSelf.fPitch,
			rSelf.globalPlayerId,
			rSelf.fleetWantedCoord,
			rSelf.uiPendingFleetWantedCoordTicks,
			rSelf.uiPendingWeaponModeTicks);
	}

	// Build-asymmetric by design: on BT_CLIENT builds == additionally folds the client-only smokeTrailId, so the
	// compiled client API compares one more field than the server's. No existing consumer compares TransferData or
	// StatusChange values, so the asymmetry is inert; if such a comparison is ever added, exclude smokeTrailId there
	// so the two builds agree.
	bool operator==(const TransferData& rOther) const
	{
		bool bEqual = SharedMembers() == rOther.SharedMembers();
#if defined(BT_CLIENT)
		bEqual = bEqual && smokeTrailId == rOther.smokeTrailId;
#endif
		return bEqual;
	}

	XMVECTOR vecPosition = DirectX::XMVectorZero();
	XMVECTOR vecDirection = DirectX::XMVectorZero();
	XMVECTOR vecVelocity = DirectX::XMVectorZero();
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
	uint16_t uiPlayerFlags = 0;

	// Spaceship timers
	float fNextBlasterSpawnTime = 0.0f;

	// Navigation delay (player transfers only)
	float fNavigationDelay = 60.0f;

	// Missile timers
	float fDeltaRotationDelay = 0.0f;
	float fTime = 0.0f;
	float fNextJitter = 0.0f;

	// Live turn rate (spaceship and missile transfers)
	float fDeltaRotation = 0.0f;
	float fDeltaRotationMax = 0.0f;
	float fPitch = 0.0f;

	// Global player ID (player transfers only)
	engine::global_id_t globalPlayerId {};

	// Fleet wanted coord (player transfers only)
	engine::GridCoord fleetWantedCoord {};

	// Pending countdown ticks (player transfers only)
	uint8_t uiPendingFleetWantedCoordTicks = 0;
	uint8_t uiPendingWeaponModeTicks = 0;

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
	UpdateFleetData    // kUpdateFleet
>;

inline StatusChangeData DefaultDataForType(StatusChangeType eType)
{
	switch (eType)
	{
		case StatusChangeType::kSpawnPlayer:      return SpawnPlayerData{};
		case StatusChangeType::kDestroyPlayer:    return DestroyPlayerData{};
		case StatusChangeType::kUpdatePlayer:      return UpdatePlayerData{};
		case StatusChangeType::kUpdateFleet:       return UpdateFleetData{};
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
