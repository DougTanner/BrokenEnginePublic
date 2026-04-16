#include "Players.h"

#include "Data/Audio.h"
#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#endif

namespace game
{

using enum PlayerFlags;

// Targeting / facing
constexpr float kfTargetRange = 80.0f;
constexpr float kfWantedDirectionSpeed = 15.0f;

// Damage response
constexpr float kfShieldHitSoundVolumeBase = 0.1f;
constexpr float kfShieldHitSoundVolumeScale = 0.1f;
constexpr float kfShieldCooldown = 2.0f;
constexpr float kfShieldDownSoundCooldown = 2.0f;
constexpr float kfShieldDownSoundVolume = 0.1f;
constexpr float kfArmorHitSoundDamageThreshold = 3.0f;
constexpr float kfArmorHitSoundVolumeBase = 0.2f;
constexpr float kfArmorHitSoundVolumeScale = 0.5f;

// Blaster spawn
constexpr float kfBlasterFireInterval = 0.05f;
constexpr float kfBlastersSpeed = 150.0f;
constexpr float kfBlastersSpawnBarrelOffset = kfPlayerRadius * 0.4667f;
constexpr float kfBlastersSpawnPreMove = kfPlayerRadius * 0.5f;
constexpr float kfBlasterAngleJitter = 0.03f;

// Missile spawn
constexpr float kfMissileSpawnInterval = 0.2f;
constexpr float kfMissileInitialVelocity = 30.0f;
constexpr float kfMissileAcceleration = 40.0f;
constexpr float kfMissileSpawnBarrelOffset = kfPlayerRadius * 0.7333f;
constexpr float kfMissileSpawnPreMove = kfPlayerRadius * 1.0f;
constexpr float kfMissileSpawnAngle = XM_PIDIV16;
constexpr float kfMissileAngleJitter = XM_PIDIV16;

// Death explosion spawn
constexpr float kfExplosionsRadius = kfPlayerRadius * 20.0f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = kfPlayerRadius * 1.3333f;
constexpr float kfExplosionSizeEnd = kfPlayerRadius * 0.3333f;
constexpr float kfExplosionSmoke = 0.25f;
constexpr float kfDeathRadialPower = 0.3f;
constexpr uint32_t kuiDeathTrailCount = 2;

// =============================================================================
// Per-player Update helpers
// =============================================================================

void XM_CALLCONV PlayersPostRender::AcquireTarget(const Frame& __restrict rPreviousFrame, FXMVECTOR vecPosition, PlayerFlags_t& rFlags, bool& rbLookTargetFound, XMVECTOR& rVecLookPosition)
{
	const SpaceshipsInterpolate& rSpaceshipsInterpolate = *rPreviousFrame.interpolate.pSpaceships;
	const SpaceshipsPostRender& rSpaceshipsPostRender = *rPreviousFrame.postRender.pSpaceships;
	int64_t iSpaceshipCount = rSpaceshipsPostRender.iCount;

	// Find nearest alive spaceship
	float fClosestDistance = kfTargetRange;
	XMVECTOR vecClosestPosition = XMVectorZero();
	bool bTargetFound = false;

	for (int64_t j = 0; j < iSpaceshipCount; ++j)
	{
		if (rSpaceshipsInterpolate.pfDestroyedTimes[j] != -1.0f)
		{
			continue;
		}

		if (rSpaceshipsPostRender.pfArrivalGracePeriods[j] > 0.0f)
		{
			continue;
		}

		if (!FrameInterpolate::IsVisible(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]))
		{
			continue;
		}

		float fDistance = common::Distance(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]);
		if (fDistance < fClosestDistance)
		{
			fClosestDistance = fDistance;
			vecClosestPosition = rSpaceshipsInterpolate.pVecPositions[j];
			bTargetFound = true;
		}
	}

	// Fallback: if no in-range target, find nearest alive spaceship for look direction
	rVecLookPosition = vecClosestPosition;
	rbLookTargetFound = bTargetFound;
	if (!rbLookTargetFound)
	{
		float fLookClosestDistance = std::numeric_limits<float>::max();
		for (int64_t j = 0; j < iSpaceshipCount; ++j)
		{
			if (rSpaceshipsInterpolate.pfDestroyedTimes[j] != -1.0f)
			{
				continue;
			}

			float fDistance = common::Distance(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]);
			if (fDistance < fLookClosestDistance)
			{
				fLookClosestDistance = fDistance;
				rVecLookPosition = rSpaceshipsInterpolate.pVecPositions[j];
				rbLookTargetFound = true;
			}
		}
	}

	// Fire flags: fire continuously at in-range targets (rate limited by weapon spawn timers)
	if (bTargetFound)
	{
		rFlags.Set(kFireBlaster);
		rFlags.Set(kFireMissile);
	}
}

void XM_CALLCONV PlayersPostRender::UpdateFacing(FXMVECTOR vecPosition, FXMVECTOR vecLookPosition, float fDeltaTime, XMVECTOR& rVecWantedDirection)
{
	XMVECTOR vecLookDirection = XMVector3Normalize(XMVectorSubtract(vecLookPosition, vecPosition));
	rVecWantedDirection = common::RotateTowardsPercent(rVecWantedDirection, vecLookDirection, common::ExponentialInterpolant(kfWantedDirectionSpeed, fDeltaTime));
}

void PlayersPostRender::RegenerateShield(float fDeltaTime, float fShieldCooldown, float& rfShield)
{
	if (fShieldCooldown <= 0.0f)
	{
		rfShield = std::min(rfShield + fDeltaTime * kfPlayerShieldRegen, kfPlayerShield);
	}
}

// =============================================================================
// Collision phases
// =============================================================================

void PlayersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

static void XM_CALLCONV ApplyDamage([[maybe_unused]] const Frame& rFrame, [[maybe_unused]] PlayersInterpolate& rPlayerInterpolate, PlayersPostRender& rPlayer, int64_t i, float fDamage, [[maybe_unused]] FXMVECTOR vecDamagePosition, [[maybe_unused]] float fHexShieldIntensity = 1.0f)
{
	// Shield absorbs damage first
	if (rPlayer.pfShields[i] > 0.0f)
	{
		// Play shield hit sound with pitch based on remaining shield
#if defined(BT_CLIENT)
		engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor465540__steaq__scifishieldhitwavwavCrc, vecDamagePosition, kfShieldHitSoundVolumeBase + kfShieldHitSoundVolumeScale * (1.0f - rPlayer.pfShields[i] / kfPlayerShield));
#endif

		// Update hex shield direction intensity
#if defined(BT_CLIENT)
		// Find lowest intensity direction slot
		int64_t iLowestIntensityIndex = 0;
		for (int64_t k = 1; k < shaders::kiHexShieldDirections; ++k)
		{
			if (rPlayerInterpolate.pHexShieldFragIntensities[i].data[k] < rPlayerInterpolate.pHexShieldFragIntensities[i].data[iLowestIntensityIndex])
			{
				iLowestIntensityIndex = k;
			}
		}
		// Store damage direction and intensity
		XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorSubtract(vecDamagePosition, rPlayerInterpolate.pVecPositions[i]));
		XMStoreFloat4A(&rPlayerInterpolate.pHexShieldDirections[i].data[iLowestIntensityIndex], vecDamageDirection);
		rPlayerInterpolate.pHexShieldVertIntensities[i].data[iLowestIntensityIndex] = fHexShieldIntensity;
		rPlayerInterpolate.pHexShieldFragIntensities[i].data[iLowestIntensityIndex] = fHexShieldIntensity;
#endif // BT_CLIENT

		float fShieldDamage = std::min(rPlayer.pfShields[i], fDamage);
		rPlayer.pfShields[i] -= fShieldDamage;
		fDamage -= fShieldDamage;

		if (rPlayer.pfShields[i] <= 0.0f)
		{
			rPlayer.pfShieldCooldowns[i] = kfShieldCooldown;

			// Play shield down sound with cooldown to prevent spam
			if (rPlayer.pfShieldDownSoundCooldowns[i] <= 0.0f)
			{
				rPlayer.pfShieldDownSoundCooldowns[i] = kfShieldDownSoundCooldown;
#if defined(BT_CLIENT)
				engine::gpAudioManager->PlayOneShot(rFrame, data::kAudioShieldArmor570852__rafaelzimrp__magicshielddownwavCrc, false, kfShieldDownSoundVolume);
#endif
			}
		}
	}

	// Remaining damage goes to armor
	if (fDamage > 0.0f)
	{
		// Play armor hit sound with pitch based on remaining armor (only for significant damage)
#if defined(BT_CLIENT)
		if (fDamage > kfArmorHitSoundDamageThreshold)
		{
			engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioShieldArmor330629__stormwaveaudio__scififorcefieldimpact15wavCrc, vecDamagePosition, kfArmorHitSoundVolumeBase + kfArmorHitSoundVolumeScale * (1.0f - rPlayer.pfArmors[i] / kfPlayerArmor));
		}
#endif

		if constexpr (!kbInvincibility)
		{
			rPlayer.pfArmors[i] -= fDamage;
		}
	}
}

void PlayersPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding)
		{
			continue;
		}

		// Flag for transfer if outside frame boundaries (Transfer phase handles removal)
		if (IsOutOfBounds(bounds, rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kTransfer);
			continue;
		}

		// Check collision results
		if (engine::Collision::HasCollision(siCollisionLayerIndex, i))
		{
			std::span<const engine::CollisionResult> collisions = engine::Collision::GetCollisions(siCollisionLayerIndex, i);
			for (const engine::CollisionResult& rResult : collisions)
			{
				if (rResult.uiOtherCategory == CollisionCategory::kSpaceship)
				{
					ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, i, kfSpaceshipCollisionDamage, rResult.vecContactPoint);
				}
				else if (rResult.uiOtherCategory == CollisionCategory::kBlaster)
				{
					ApplyDamage(rFrame, rCurrentInterpolate, rCurrentPostRender, i, rResult.fDamageReceived, rResult.vecContactPoint);

					// Spawn impact VFX at contact point
#if defined(BT_CLIENT)
					engine::PuffsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayersInterpolate::suiImpactPuffControllerTypeIndex, rResult.vecContactPoint);
					engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, PlayersInterpolate::suiImpactPointLightControllerTypeIndex, rResult.vecContactPoint, 0.0f);
#endif
				}
			}
		}

		// Check for death
		if (rCurrentPostRender.pfArmors[i] <= 0.0f)
		{
			rCurrentPostRender.pFlags[i].Set(kExploding);
			rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;
			rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;
		}
	}
}

// =============================================================================
// Weapon and death-explosion spawn helpers (called from PlayersPostRender::Spawn)
// =============================================================================

void PlayersPostRender::SpawnBlasters([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kFireBlaster))
		{
			continue;
		}
		rCurrentPostRender.pFlags[i].Clear(kFireBlaster);

		if (rCurrentPostRender.pFlags[i] & kUseMissiles)
		{
			continue;
		}

		// Calculate base blaster direction and barrel offset normal
		XMVECTOR vecBaseDirection = rCurrentInterpolate.pVecDirections[i];
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));

		// Decrement timer and spawn multiple blasters if needed
		rCurrentPostRender.pfNextBlasterFireTimes[i] -= fDeltaTime;

		while (rCurrentPostRender.pfNextBlasterFireTimes[i] <= 0.0f)
		{
			// Inter-frame time: how much time has elapsed since this blaster should have spawned
			float fInterFrameTime = -rCurrentPostRender.pfNextBlasterFireTimes[i];

			// Interpolate player position backwards to where they were when this blaster spawned
			XMVECTOR vecPlayerPositionAtSpawn = rCurrentInterpolate.pVecPositions[i] - fInterFrameTime * rCurrentPostRender.pVecVelocities[i];

			// Alternate barrels
			rCurrentPostRender.pFlags[i].Toggle(kBlasterSpawnLeft);
			float fBarrelOffset = (rCurrentPostRender.pFlags[i] & kBlasterSpawnLeft) ? kfBlastersSpawnBarrelOffset : -kfBlastersSpawnBarrelOffset;

			// Apply random angle jitter to this blaster's direction
			XMVECTOR vecJitteredDirection = common::RandomAngleJitter(vecBaseDirection, kfBlasterAngleJitter, rFrame.postRender.randomEngine);
			XMVECTOR vecBlasterVelocity = kfBlastersSpeed * vecJitteredDirection;

			// Calculate spawn position: player position at spawn time + barrel offset + pre-move along velocity
			XMVECTOR vecSpawnPosition = vecPlayerPositionAtSpawn + fBarrelOffset * vecLeftNormal;
			XMVECTOR vecFinalPosition = vecSpawnPosition + kfBlastersSpawnPreMove * vecJitteredDirection + fInterFrameTime * vecBlasterVelocity;

			// Spawn blaster with calculated position and velocity
			BlastersPostRender::Spawn(rFrame,
			{
				.vecPosition = vecFinalPosition,
				.vecVelocity = vecBlasterVelocity,
				.uiTypeIndex = PlayersInterpolate::suiBlasterTypeIndex,
				.alignment = rCurrentPostRender.pAlignments[i],
				.fWindTrailIntensity = game::gWindDepositPlayerBlastersIntensity.Get(),
				.fWindTrailWidth = game::gWindDepositPlayerBlastersWidth.Get(),
				.fWindTrailLengthMultiplier = game::gWindDepositBlastersLengthMultiplier.Get(),
			});

			rCurrentPostRender.pfNextBlasterFireTimes[i] += kfBlasterFireInterval;
		}
	}
}

void PlayersPostRender::SpawnMissiles([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Always decrement timer so releasing and re-pressing fires immediately after cooldown
		rCurrentPostRender.pfNextSecondarySpawnTimes[i] -= fDeltaTime;

		if (!(rCurrentPostRender.pFlags[i] & kFireMissile))
		{
			continue;
		}
		rCurrentPostRender.pFlags[i].Clear(kFireMissile);

		if (!(rCurrentPostRender.pFlags[i] & kUseMissiles))
		{
			continue;
		}

		if (rCurrentPostRender.pfNextSecondarySpawnTimes[i] >= 0.0f || (rCurrentPostRender.pFlags[i] & kExploding))
		{
			continue;
		}

		rCurrentPostRender.pfNextSecondarySpawnTimes[i] = kfMissileSpawnInterval;

		// Toggle spawn side
		rCurrentPostRender.pFlags[i].Toggle(kMissileSpawnLeft);
		bool bLeftSide = rCurrentPostRender.pFlags[i] & kMissileSpawnLeft;

		// Base direction is player's smoothed visual direction
		XMVECTOR vecBaseDirection = rCurrentInterpolate.pVecDirections[i];

		// Calculate barrel offset normal (perpendicular to facing direction)
		XMVECTOR vecLeftNormal = XMVector3Normalize(XMVector3Cross(vecBaseDirection, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
		float fBarrelOffset = bLeftSide ? kfMissileSpawnBarrelOffset : -kfMissileSpawnBarrelOffset;

		// Calculate angled firing direction with jitter (angles outward from center)
		float fAngleOffset = bLeftSide ? -kfMissileSpawnAngle : kfMissileSpawnAngle;
		XMVECTOR vecAngledDirection = XMVector3TransformNormal(vecBaseDirection, XMMatrixRotationZ(fAngleOffset));
		XMVECTOR vecJitteredDirection = common::RandomAngleJitter(vecAngledDirection, kfMissileAngleJitter, rFrame.postRender.randomEngine);

		// Calculate spawn position: barrel offset + pre-move along jittered direction
		XMVECTOR vecSpawnPosition = rCurrentInterpolate.pVecPositions[i] + fBarrelOffset * vecLeftNormal;
		XMVECTOR vecMissilePosition = vecSpawnPosition + kfMissileSpawnPreMove * vecJitteredDirection;
		XMVECTOR vecMissileVelocity = XMVectorReplicate(kfMissileInitialVelocity) * vecJitteredDirection;

		// Spawn with stored direction = player's wanted direction (for untargeted orientation)
		MissilesPostRender::Spawn(rFrame,
		{
			.vecPosition = vecMissilePosition,
			.vecDirection = vecJitteredDirection,
			.vecVelocity = vecMissileVelocity,
			.vecStoredDirection = vecBaseDirection,
			.uiTarget = Frame::GetMissileTarget(rFrame, vecMissilePosition, vecBaseDirection, rCurrentPostRender.pAlignments[i]),
			.fAcceleration = kfMissileAcceleration,
			.flags = MissileFlags::kTargetEnemy,
			.alignment = rCurrentPostRender.pAlignments[i],
		});
	}
}

void PlayersPostRender::SpawnDeathExplosions([[maybe_unused]] Frame& __restrict rFrame)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentPostRender.pfDestroyedExplosionTimes[i] <= 0.0f && rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f))
		{
			continue;
		}

		rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;

		float fPercent = rCurrentInterpolate.pfDestroyedTimes[i] / kfDestroyTime;

		// Random direction for explosion
		XMVECTOR vecDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(common::Random<XM_2PI>(rFrame.postRender.randomEngine)));

		XMVECTOR vecJitteredPosition = common::RandomPositionJitter<1.0f>(rCurrentInterpolate.pVecPositions[i], rFrame.postRender.randomEngine);
		XMVECTOR vecJitteredDirection = common::RandomDirectionJitter<0.5f>(vecDirection, rFrame.postRender.randomEngine);

		// Radial offset based on time
		float fAdjustedPercent = (std::pow((1.0f - fPercent) + 1.0f, kfDeathRadialPower) - 1.0f) * kfExplosionsRadius;
		vecJitteredPosition = XMVectorMultiplyAdd(vecJitteredDirection, XMVectorReplicate(fAdjustedPercent), vecJitteredPosition);

		engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
		{
			.uiTypeIndex = PlayersInterpolate::suiExplosionTypeIndex,
			.vecPosition = vecJitteredPosition,
			.vecDirection = vecJitteredDirection,
			.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kYellow},
			.uiTrailCount = kuiDeathTrailCount,
			.fTrailAngle = fPercent * XM_PIDIV2,
			.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
			.fParticleAngle = fPercent * XM_PIDIV2,
			.fLightPercent = fPercent * kfExplosionIntensity,
			.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
			.fSmokePercent = fPercent * kfExplosionSmoke,
			.fTimePercent = fPercent,
		});
	}
}

} // namespace game
