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

struct TransferData
{
	bool operator==(const TransferData& rOther) const
	{
		return XMVector4Equal(vecPosition, rOther.vecPosition) &&
			XMVector4Equal(vecDirection, rOther.vecDirection) &&
			XMVector4Equal(vecVelocity, rOther.vecVelocity) &&
			alignment == rOther.alignment &&
			fHealth == rOther.fHealth &&
			fShield == rOther.fShield &&
			uiTypeIndex == rOther.uiTypeIndex &&
			fWindTrailIntensity == rOther.fWindTrailIntensity &&
			fWindTrailWidth == rOther.fWindTrailWidth &&
			fWindTrailLengthMultiplier == rOther.fWindTrailLengthMultiplier &&
			fAcceleration == rOther.fAcceleration &&
			fNextBlasterFireTime == rOther.fNextBlasterFireTime &&
			fNextSecondarySpawnTime == rOther.fNextSecondarySpawnTime &&
			fShieldCooldown == rOther.fShieldCooldown &&
			fShieldDownSoundCooldown == rOther.fShieldDownSoundCooldown &&
			fAnimationTime == rOther.fAnimationTime &&
			fShieldRotation == rOther.fShieldRotation &&
			fShieldShrink == rOther.fShieldShrink &&
			uiPlayerFlags == rOther.uiPlayerFlags &&
			fNextBlasterSpawnTime == rOther.fNextBlasterSpawnTime &&
			fDeltaRotationDelay == rOther.fDeltaRotationDelay &&
			fTime == rOther.fTime &&
			fExhaustDelay == rOther.fExhaustDelay &&
			fNextJitter == rOther.fNextJitter
#if defined(BT_CLIENT)
			&& smokeTrailId == rOther.smokeTrailId
#endif
			;
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
