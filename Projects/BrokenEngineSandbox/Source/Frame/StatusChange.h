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
	}
	return "Unknown";
}

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
			rSelf.fDeltaRotationDelay, rSelf.fTime, rSelf.fExhaustDelay, rSelf.fNextJitter);
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

	// Missile timers
	float fDeltaRotationDelay = 0.0f;
	float fTime = 0.0f;
	float fExhaustDelay = 0.0f;
	float fNextJitter = 0.0f;

	// Smoke trail ID reuse (missiles only)
#if defined(BT_CLIENT)
	engine::smoke_trails_t smokeTrailId {};
#endif
};

struct StatusChange
{
	bool operator==(const StatusChange&) const = default;

	StatusChangeType eType {};
	TransferData data {};
};

} // namespace game
