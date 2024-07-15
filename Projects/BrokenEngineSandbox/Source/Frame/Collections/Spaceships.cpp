// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Spaceships.h"

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Game.h"

using namespace DirectX;

using enum engine::BillboardFlags;
using enum engine::ExplosionFlags;
using enum engine::TargetFlags;

namespace game
{

using enum BlasterFlags;
using enum SpaceshipFlags;

constexpr float kfFreezeTimeBlaster = 0.025f;

constexpr float kfVelocityDecay = 0.25f;
constexpr float kfVelocityToDirection = 4.0f;
constexpr float kfAccelerationTowardsPlayer = 4.0f;
constexpr float kfFleePlayerAcceleration = 6.0f;
constexpr float kfReturnToIslandCenterAcceleration = 10.0f;

constexpr float kfDeltaAngleMax = 4.0f;
constexpr float kfDeltaAngleChange = 0.999f;
constexpr float kfDeltaAngleChangeAvoidTerrain = 0.995f;
constexpr float kfDeltaAngleDecay = 6.0f;
constexpr float kfRoll = 0.2f;

constexpr float kfDeltaAngleTowardsPlayer = 32.0f;
constexpr float kfFleePlayerDeltaAngle = 32.0f;

constexpr float kfFleePlayerStart = 15.0f;
constexpr float kfFleePlayerEnd = 25.0f;
constexpr float kfReturnDistance = 180.0f;
constexpr float kfReturnedDistance = kfReturnDistance - 20.0f;

constexpr int64_t kiFrontSamples = 4;
constexpr float kfFrontSamplesStep = 4.0f;
constexpr int64_t kiSideSamples = 2;
constexpr float kfSideSamplesStep = 2.0f;
constexpr float kfStepReduceWeight = 0.1f;
constexpr float kfAvoidTerrainMin = 0.5f;
constexpr float kfAvoidTerrainMax = 2.5f;
constexpr float kfAvoidTerrainAccelerationMax = 0.25f;
constexpr float kfAvoidTerrainDeltaAngleMin = 16.0f;
constexpr float kfAvoidTerrainDeltaAngleMax = 32.0f;
constexpr float kfIgnoreAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfIgnoreAvoidTerrainPlayerDistance = 40.0f;

constexpr float kfTerrainCollisionRotation = 8.0f;
constexpr float kfTerrainCollisionMovePosition = 4.0f;
constexpr float kfTerrainCollisionAddVelocity = 4.0f;

constexpr float kfPusherRadius = 1.5f;
constexpr float kfPusherIntensity = 400.0f;
constexpr float kfPusherPower = 5.0f;

constexpr float kfPlayerCollisionRadius = 2.0f;
constexpr float kfBlasterCollisionRadius = 1.75f;
constexpr float kfHealthRegen = 0.1f;
constexpr float kfDamageTrailOffset = -0.8f;
constexpr float kfDamageTrailIntensity = 0.02f;
constexpr float kfDamageTrailJitter = 0.2f;

constexpr float kfDestroyTime = 0.25f;
constexpr float kfDestroyVelocity = 15.0f;
constexpr float kfDestroyExplosionInterval = 0.024f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 16.0f;
constexpr float kfExplosionSizeStart = 1.25f;
constexpr float kfExplosionSizeEnd = 0.75f;
constexpr float kfExplosionSmoke = 0.5f;

// Blasters
constexpr float kfSpawnBlasterPlayerAngle = 0.1f;
constexpr float kfBlastersSpawnInterval = 0.085f;
constexpr float kfBlastersSpawnCooldown = 1.0f;

constexpr float kfBlastersSpawnPreMove = 0.01f;
constexpr float kfBlastersSpawnPositionJitter = 0.0f;
constexpr float kfBlastersWidth = 0.25f;
constexpr float kfBlastersLength = 0.55f;
constexpr float kfBlasterVisibleIntensity = 1.25f;
constexpr float kfBlasterLightingArea = 2.0f;
constexpr float kfBlasterLightingIntensity = 2000.0f;

bool Spaceships::operator==(const Spaceships& rOther) const
{
	bool bEqual = true;

	bEqual &= iCount == rOther.iCount;
	bEqual &= iKilled == rOther.iKilled;

	common::BreakOnNotEqual(bEqual);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= pFlags[i] == rOther.pFlags[i];
		bEqual &= pVecPositions[i] == rOther.pVecPositions[i];
		bEqual &= pVecDirections[i] == rOther.pVecDirections[i];
		bEqual &= puiPushers[i] == rOther.puiPushers[i];
		bEqual &= puiTargets[i] == rOther.puiTargets[i];
		bEqual &= puiDamageTrails[i] == rOther.puiDamageTrails[i];
		bEqual &= puiBillboards[i] == rOther.puiBillboards[i];
		bEqual &= pfDestroyedTimes[i] == rOther.pfDestroyedTimes[i];

		bEqual &= pVecVelocities[i] == rOther.pVecVelocities[i];
		bEqual &= pfDeltaRotations[i] == rOther.pfDeltaRotations[i];
		bEqual &= pfHealths[i] == rOther.pfHealths[i];
		bEqual &= pfFreezeTimes[i] == rOther.pfFreezeTimes[i];
		bEqual &= pfDestroyedExplosionTimes[i] == rOther.pfDestroyedExplosionTimes[i];
		bEqual &= pfNextBlasterSpawnTimes[i] == rOther.pfNextBlasterSpawnTimes[i];
		bEqual &= piBlasterSpawns[i] == rOther.piBlasterSpawns[i];

		common::BreakOnNotEqual(bEqual);
	}

	return bEqual;
}

void Spaceships::Copy(int64_t iDestIndex, int64_t iSrcIndex)
{
	pFlags[iDestIndex] = pFlags[iSrcIndex];
	pVecPositions[iDestIndex] = pVecPositions[iSrcIndex];
	pVecDirections[iDestIndex] = pVecDirections[iSrcIndex];
	puiPushers[iDestIndex] = puiPushers[iSrcIndex];
	puiTargets[iDestIndex] = puiTargets[iSrcIndex];
	puiDamageTrails[iDestIndex] = puiDamageTrails[iSrcIndex];
	puiBillboards[iDestIndex] = puiBillboards[iSrcIndex];
	pfDestroyedTimes[iDestIndex] = pfDestroyedTimes[iSrcIndex];

	pVecVelocities[iDestIndex] = pVecVelocities[iSrcIndex];
	pfDeltaRotations[iDestIndex] = pfDeltaRotations[iSrcIndex];
	pfHealths[iDestIndex] = pfHealths[iSrcIndex];
	pfFreezeTimes[iDestIndex] = pfFreezeTimes[iSrcIndex];
	pfDestroyedExplosionTimes[iDestIndex] = pfDestroyedExplosionTimes[iSrcIndex];
	pfNextBlasterSpawnTimes[iDestIndex] = pfNextBlasterSpawnTimes[iSrcIndex];
	piBlasterSpawns[iDestIndex] = piBlasterSpawns[iSrcIndex];
}

void Spaceships::Global([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
}

void Spaceships::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Spaceships& rCurrent = rFrame.spaceships;
	const Spaceships& rPrevious = rPreviousFrame.spaceships;

	// 1. operator== 2. Copy() 3. Load/Save in Global() or Main() or PostRender() 4. Spawn()
	// Make sure to Remove() any pools in Destroy()
	VERIFY_SIZE(rCurrent, 87104);

	rCurrent.iCount = rPrevious.iCount;
	rCurrent.iKilled = rPrevious.iKilled;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		SpaceshipFlags_t flags = rPrevious.pFlags[i];
		auto vecPosition = rPrevious.pVecPositions[i];
		auto vecDirection = rPrevious.pVecDirections[i];
		engine::pusher_t uiPusher = rPrevious.puiPushers[i];
		engine::target_t uiTarget = rPrevious.puiTargets[i];
		engine::trail_t uiDamageTrail = rPrevious.puiDamageTrails[i];
		engine::billboard_t uiBillboard = rPrevious.puiBillboards[i];
		float fDestroyedTime = std::max(0.0f, rPrevious.pfDestroyedTimes[i] - fDeltaTime);

		// Add velocity to position
		if (rPrevious.pfFreezeTimes[i] <= 0.0f) [[likely]]
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPrevious.pVecVelocities[i], vecPosition);
		}

		ASSERT(DirectX::XMVectorGetW(vecPosition) == 1.0f);

		// Update pusher
		rFrame.pushers.Add(uiPusher,
		{
			.f2Position = {XMVectorGetX(vecPosition), XMVectorGetY(vecPosition)},
			.fRadius = kfPusherRadius,
			.fIntensity = kfPusherIntensity,
			.fPower = kfPusherPower,
		});

		// Add delta rotation to direction
		vecDirection = XMVector3Normalize(XMVector4Transform(vecDirection, XMMatrixRotationZ(fDeltaTime * rPrevious.pfDeltaRotations[i])));

		// Update target position
		if (!(flags & kExploding)) [[unlikely]]
		{
			rFrame.targets.Add(uiTarget,
			{
				.flags = {kDestination, kTargetIsEnemy},
				.vecPosition = vecPosition,
			});
		}
		else
		{
			rFrame.targets.Remove(rFrame, uiTarget, {kDestination});
		}

		// Update damage trail
		if (rPrevious.pfHealths[i] <= 0.5f * EnemyHealthMultiplier(rFrame) * kfSpaceshipHealth) [[unlikely]]
		{
			float fPercent = rPrevious.pfHealths[i] / (0.5f * EnemyHealthMultiplier(rFrame) * kfSpaceshipHealth);

			auto vecTrailPosition = XMVectorMultiplyAdd(XMVectorReplicate(kfDamageTrailOffset), vecDirection, vecPosition);
			vecTrailPosition = XMVectorAdd(XMVectorSet(-kfDamageTrailJitter + common::Random<2.0f * kfDamageTrailJitter>(rFrame.randomEngine), -kfDamageTrailJitter + common::Random<2.0f * kfDamageTrailJitter>(rFrame.randomEngine), 0.0f, 0.0f), vecTrailPosition);

			rFrame.trails.Add(uiDamageTrail, rFrame.fCurrentTime,
			{
				.vecPosition = vecTrailPosition,
				.fIntensity = kfDamageTrailIntensity * (1.0f - fPercent) * (1.0f - fPercent),
			});
		}
		else if (uiDamageTrail != 0) [[unlikely]]
		{
			rFrame.trails.Remove(uiDamageTrail);
		}

		// Offscreen arrow
		rFrame.billboards.Add(uiBillboard,
		{
			.flags = {kOffscreenOnly, kOffscreenRotate, kTypeNone},
			.crc = data::kTexturesSpaceshipsBC7EnemyOffscreenpngCrc,
			.fSize = 0.025f,
			.fAlpha = 0.75f,
			.fRotation = 0.0f,
			.vecPosition = rCurrent.pVecPositions[i],
		});

		// Save
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.puiPushers[i] = uiPusher;
		rCurrent.puiTargets[i] = uiTarget;
		rCurrent.puiDamageTrails[i] = uiDamageTrail;
		rCurrent.puiBillboards[i] = uiBillboard;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
	}
}

void XM_CALLCONV SpawnSpaceshipExplosion(Frame& __restrict rFrame, int64_t i, float fPercent, FXMVECTOR vecDirection)
{
	Spaceships& rCurrent = rFrame.spaceships;

	static constexpr float kfPositionJitter = 0.75f;
	auto vecPosition = XMVectorAdd(XMVectorSet(-kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.randomEngine), -kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.randomEngine), 0.0f, 0.0f), rCurrent.pVecPositions[i]);

	static constexpr float kfDirectionJitter = 0.5f;
	auto vecFinalDirection = XMVector3Normalize(XMVectorAdd(XMVectorSet(-kfDirectionJitter + common::Random<2.0f * kfDirectionJitter>(rFrame.randomEngine), -kfDirectionJitter + common::Random<2.0f * kfDirectionJitter>(rFrame.randomEngine), 0.0f, 0.0f), vecDirection));

	engine::explosion_t uiExplosion = 0;
	rFrame.explosions.Add(uiExplosion, rFrame,
	{
		.flags = {kDestroysSelf, kRed},
		.vecPosition = vecPosition,
		.vecDirection = vecFinalDirection,
		.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
		.fParticleAngle = fPercent * XM_PIDIV2,
		.uiTrailCount = 2,
		.fTrailAngle = fPercent * XM_PIDIV2,
		.fLightPercent = fPercent * kfExplosionIntensity,
		.fPusherPercent = 0.0f,
		.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
		.fSmokePercent = fPercent * kfExplosionSmoke,
		.fTimePercent = fPercent,
	});
}

void Spaceships::PostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	SCOPED_CPU_PROFILE(engine::kCpuTimerPostRenderSpaceships);

	Spaceships& rCurrent = rFrame.spaceships;
	const Spaceships& rPrevious = rPreviousFrame.spaceships;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		auto vecVelocity = rPrevious.pVecVelocities[i];
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fHealth = rPrevious.pfHealths[i];
		float fFreezeTime = rPrevious.pfFreezeTimes[i] - fDeltaTime;
		float fDestroyedExplosionTime = rPrevious.pfDestroyedExplosionTimes[i] - fDeltaTime;
		float fNextBlasterSpawnTime = rPrevious.pfNextBlasterSpawnTimes[i];
		int32_t iBlasterSpawn = rPrevious.piBlasterSpawns[i];

		// Slowly regen health if offscreen and not destroyed
		if (!(rCurrent.pFlags[i] & kExploding) && common::Distance(rCurrent.pVecPositions[i], rFrame.player.vecPosition) > 60.0f) [[unlikely]]
		{
			fHealth = std::min(fHealth + fDeltaTime * kfHealthRegen, EnemyHealthMultiplier(rFrame) * kfSpaceshipHealth);
		}

		// Check if spaceship should flee or stop fleeing player
		auto vecToPlayer = XMVectorSubtract(rFrame.player.vecPosition, rCurrent.pVecPositions[i]);
		float fPlayerDistance = XMVectorGetX(XMVector3Length(vecToPlayer));
		if (fPlayerDistance < kfFleePlayerStart)
		{
			rCurrent.pFlags[i] |= kFleePlayer;
		}
		else if (fPlayerDistance > kfFleePlayerEnd)
		{
			rCurrent.pFlags[i] &= kFleePlayer;
		}

		// Update returning to island center
		auto vecIslandCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		float fDistanceFromIslandCenter = common::Distance(rCurrent.pVecPositions[i], vecIslandCenter);
		if (fDistanceFromIslandCenter > kfReturnDistance)
		{
			rCurrent.pFlags[i] |= kReturnToIslandCenter;
		}
		else if (fDistanceFromIslandCenter < kfReturnedDistance)
		{
			rCurrent.pFlags[i] &= kReturnToIslandCenter;
		}

		// Select destination
		auto vecDestination = rFrame.player.vecPosition;
		if (rCurrent.pFlags[i] & kReturnToIslandCenter)
		{
			vecDestination = vecIslandCenter;
		}

		// Update delta rotation towards destination
		auto vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(vecDestination, rCurrent.pVecPositions[i]));
		float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(rCurrent.pVecDirections[i], vecToDestinationNormal));
		float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaAngleTowardsPlayer : -kfDeltaAngleTowardsPlayer;
		if (!(rCurrent.pFlags[i] & kReturnToIslandCenter) && rCurrent.pFlags[i] & kFleePlayer)
		{
			fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? -kfFleePlayerDeltaAngle : kfFleePlayerDeltaAngle;
		}

		fDeltaRotation = kfDeltaAngleChange * fDeltaRotation + (1.0f - kfDeltaAngleChange) * fWantedDeltaRotation;

		fDeltaRotation = (1.0f - fDeltaTime * kfDeltaAngleDecay) * fDeltaRotation;

		// Decay velocity
		vecVelocity = XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfVelocityDecay), vecVelocity);

		// Accelerate
		if (!(rCurrent.pFlags[i] & kExploding)) [[likely]]
		{
			// Add acceleration to velocity
			float fAcceleration = rCurrent.pFlags[i] & kReturnToIslandCenter ? kfReturnToIslandCenterAcceleration : rCurrent.pFlags[i] & kFleePlayer ? kfFleePlayerAcceleration : kfAccelerationTowardsPlayer;
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAcceleration), rCurrent.pVecDirections[i], vecVelocity);

			// Rotate velocity towards direction
			float fPercent = 1.0f - fDeltaTime * kfVelocityToDirection;
			auto vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fPercent), XMVector3Normalize(vecVelocity));
			auto vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fPercent), rCurrent.pVecDirections[i]);
			vecVelocity = XMVectorMultiply(XMVector3Length(vecVelocity), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));
		}

		// Collide terrain
		float fTerrainElevation = engine::gpIslands->GlobalElevation(rCurrent.pVecPositions[i]);
		if (fTerrainElevation >= XMVectorGetZ(rCurrent.pVecPositions[i])) [[unlikely]]
		{
			auto vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(rCurrent.pVecPositions[i]), 0.0f));

			// Move away from terrain a bit
			rCurrent.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionMovePosition), vecTerrainNormal, rCurrent.pVecPositions[i]);

			// Set a rotation away from terrain
			float fDirectionTerrainCrossZ = XMVectorGetZ(XMVector3Cross(rCurrent.pVecDirections[i], vecTerrainNormal));
			fDeltaRotation = fDirectionTerrainCrossZ > 0.0f ? kfTerrainCollisionRotation : -kfTerrainCollisionRotation;

			// Reflect velocity around terrain normal
			vecVelocity = XMVector3Reflect(vecVelocity, vecTerrainNormal);

			// Add velocity away from terrain
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionAddVelocity), vecTerrainNormal, vecVelocity);
		}

		// Spawn explosions
		if (rCurrent.pFlags[i] & kExploding && fDestroyedExplosionTime < 0.0f) [[unlikely]]
		{
			fDestroyedExplosionTime = kfDestroyExplosionInterval;

			float fPercent = rCurrent.pfDestroyedTimes[i] / kfDestroyTime;
			SpawnSpaceshipExplosion(rFrame, i, fPercent, XMVector3Normalize(vecVelocity));
		}

		// Spawn damage particles
		if (!(rCurrent.pFlags[i] & kExploding) && fHealth < 0.5f)
		{
			float fPercent = rPrevious.pfHealths[i] / (0.5f * EnemyHealthMultiplier(rFrame) * kfSpaceshipHealth);
			SpawnDamageParticles(rFrame, rCurrent.pVecPositions[i], rCurrent.pVecDirections[i], fPercent);
		}

		// Fire blasters?
		auto vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
		float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrent.pVecDirections[i], vecToPlayerNormal));

		bool bSpawnBlaster = fAngleToPlayer <= kfSpawnBlasterPlayerAngle && engine::InsideVisibleArea(rFrameInput, rCurrent.pVecPositions[i], kfEnemyFireAreaXAdjust, kfEnemyFireAreaXAdjust, kfEnemyFireVisibleAreaYAdjustTop, kfEnemyFireVisibleAreaYAdjustBottom);

		fNextBlasterSpawnTime -= fDeltaTime;
		if (fNextBlasterSpawnTime < 0.0f && (bSpawnBlaster || iBlasterSpawn != 2))
		{
			if (iBlasterSpawn == 1 || iBlasterSpawn == 2)
			{
				--iBlasterSpawn;
				float fBlastersSpawnInterval = kfBlastersSpawnInterval;
				fNextBlasterSpawnTime = fBlastersSpawnInterval;
			}
			else
			{
				iBlasterSpawn = 2;
				fNextBlasterSpawnTime = kfBlastersSpawnCooldown;
			}

			auto vecDirection = rCurrent.pVecDirections[i];
			auto vecBlasterVelocity = XMVectorMultiply(XMVectorSet(kfBlastersSpeed, kfBlastersSpeed, 0.0f, 0.0f), XMVector3Normalize(vecDirection));
			auto vecPosition = XMVectorAdd(rCurrent.pVecPositions[i] + kfBlastersSpawnPreMove * vecBlasterVelocity, XMVectorSet(-kfBlastersSpawnPositionJitter + common::Random<2.0f * kfBlastersSpawnPositionJitter>(rFrame.randomEngine), -kfBlastersSpawnPositionJitter + common::Random<2.0f * kfBlastersSpawnPositionJitter>(rFrame.randomEngine), 0.0f, 0.0f));

			rFrame.blasters.AddSpawn(
			{
				.flags = kCollidePlayer,
				.crc = data::kTexturesBlasterBC77pngCrc,
				.vecPosition = vecPosition,
				.vecVelocity = vecBlasterVelocity,
				.f2Size = {kfBlastersWidth, kfBlastersLength},
				.fVisibleIntensity = kfBlasterVisibleIntensity,
				.fLightArea = kfBlasterLightingArea,
				.fLightIntensity = kfBlasterLightingIntensity,
				.fDamage = Damage(kDamageSpaceshipBlaster),
			});
		}

		// Save
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfHealths[i] = fHealth;
		rCurrent.pfFreezeTimes[i] = fFreezeTime;
		rCurrent.pfDestroyedExplosionTimes[i] = fDestroyedExplosionTime;
		rCurrent.pfNextBlasterSpawnTimes[i] = fNextBlasterSpawnTime;
		rCurrent.piBlasterSpawns[i] = iBlasterSpawn;
	}

	// Make sure to do pushers before other position modifiers (or it will push itself)
	Multithread<256>(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, rCurrent.iCount, &Spaceships::PostRenderPushers, engine::kCpuTimerPostRenderSpaceshipsPushers);

	Multithread<256>(rFrame, rPreviousFrame, rFrameInput, fDeltaTime, rCurrent.iCount, &Spaceships::PostRenderAvoidTerrain, engine::kCpuTimerPostRenderSpaceshipsAvoidTerrain);

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		rCurrent.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rFrame.pullers.ApplyPull(rCurrent.pVecPositions[i]), rCurrent.pVecPositions[i]);
		ASSERT(DirectX::XMVectorGetW(rCurrent.pVecPositions[i]) == 1.0f);
	}
}

// WARNING: This function is multithreaded
void Spaceships::PostRenderAvoidTerrain([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime, int64_t iStart, int64_t iEnd)
{
	Spaceships& rCurrent = rFrame.spaceships;

	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		auto vecToPlayer = XMVectorSubtract(rFrame.player.vecPosition, rCurrent.pVecPositions[i]);
		float fDistanceToPlayer = XMVectorGetX(XMVector3Length(vecToPlayer));
		if (fDistanceToPlayer < kfIgnoreAvoidTerrainPlayerDistance)
		{
			auto vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
			float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrent.pVecDirections[i], vecToPlayerNormal));
			if (fAngleToPlayer < kfIgnoreAvoidTerrainPlayerAngle)
			{
				continue;
			}
		}

		// Add avoid terrain factor to wanted delta rotation
		auto vecLeftDirection = XMVector3Cross(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rCurrent.pVecDirections[i]);
		float fLeftElevation = 0.0f;
		float fRightElevation = 0.0f;
		float fTotalWeight = 0.0f;
		for (int64_t j = 0; j < kiFrontSamples; ++j)
		{
			float fWeightFront = 1.0f - static_cast<float>(j) * kfStepReduceWeight;
			auto vecSamplePosition = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(j + 1) * kfFrontSamplesStep), rCurrent.pVecDirections[i], rCurrent.pVecPositions[i]);

			for (int64_t k = 0; k < kiSideSamples; ++k)
			{
				float fWeight = fWeightFront - static_cast<float>(k) * kfStepReduceWeight;
				ASSERT(fWeight > 0.0f);
				fTotalWeight += fWeight;

				auto vecSamplePositionLeft = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
				fLeftElevation += fWeight * engine::gpIslands->GlobalElevation(vecSamplePositionLeft);
				auto vecSamplePositionRight = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * -kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
				fRightElevation += fWeight * engine::gpIslands->GlobalElevation(vecSamplePositionRight);
			}
		}
		float fTotalWeightInverse = 1.0f / fTotalWeight;
		fLeftElevation *= fTotalWeightInverse;
		fRightElevation *= fTotalWeightInverse;

		if (fLeftElevation > kfAvoidTerrainMin || fRightElevation > kfAvoidTerrainMin)
		{
			float fPercent = fLeftElevation > fRightElevation ? (fLeftElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax : (fRightElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax;
			
			float fAvoidDeltaAngle = (1.0f - fPercent) * kfAvoidTerrainDeltaAngleMin + fPercent * kfAvoidTerrainDeltaAngleMax;
			float fWantedDeltaRotation = fLeftElevation > fRightElevation ? -fAvoidDeltaAngle : fAvoidDeltaAngle;
			rCurrent.pfDeltaRotations[i] = kfDeltaAngleChangeAvoidTerrain * rCurrent.pfDeltaRotations[i] + (1.0f - kfDeltaAngleChangeAvoidTerrain) * fWantedDeltaRotation;
		}

		rCurrent.pfDeltaRotations[i] = common::MinAbs(rCurrent.pfDeltaRotations[i], kfDeltaAngleMax);
	}
}

void Spaceships::PostRenderPushers([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime, int64_t iStart, int64_t iEnd)
{
	Spaceships& rCurrent = rFrame.spaceships;

	// WARNING: This function is multithreaded
	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		if (!(rCurrent.pFlags[i] & kExploding)) [[likely]]
		{
			rCurrent.pVecVelocities[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rFrame.pushers.ApplyPush(rCurrent.pVecPositions[i]), rCurrent.pVecVelocities[i]);
		}

		// In some spawn situations, spaceships can get compressed and accelerated too much?
		// They fly in from offscreen at super speed
		rCurrent.pVecVelocities[i] = XMVectorClamp(rCurrent.pVecVelocities[i], XMVectorReplicate(-30.0f), XMVectorReplicate(30.0f));
	}
}

void XM_CALLCONV Spaceships::Explode(Frame& __restrict rFrame, int64_t i, [[maybe_unused]] FXMVECTOR vecDirection)
{
	Spaceships& rCurrent = rFrame.spaceships;

	if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
	{
		DEBUG_BREAK();
		return;
	}

	++rCurrent.iKilled;

	engine::gpAudioManager->PlayOneShot(data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrent.pVecPositions[i], 0.3f);

	rCurrent.pFlags[i] |= kExploding;
	rCurrent.pfDestroyedTimes[i] = kfDestroyTime;
	rCurrent.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;
	rCurrent.pVecVelocities[i] = XMVectorMultiply(XMVectorReplicate(kfDestroyVelocity), vecDirection);

	SpawnSpaceshipExplosion(rFrame, i, 1.0f, XMVector3Normalize(vecDirection));

	Frame::SpawnPickup(rFrame, rCurrent.pVecPositions[i], kfSpaceshipArmorShardChance);
}

void Spaceships::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Spaceships& rCurrent = rFrame.spaceships;

	// Collide with player
	if (rFrame.player.fArmor > 0.0f && rFrame.player.fSkillTime <= 0.0f) [[likely]]
	{
		constexpr float kfCollisionRadius = 2.0f;
		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
			{
				continue;
			}

			auto vecToPlayer = XMVectorSubtract(rFrame.player.vecPosition, rCurrent.pVecPositions[i]);
			float fLength = XMVectorGetX(XMVector3Length(vecToPlayer));
			if (fLength > kfCollisionRadius) [[likely]]
			{
				continue;
			}

			Explode(rFrame, i, XMVector3Normalize(vecToPlayer));
			Player::Damage(rFrame, Damage(kDamageSpaceshipCollision), rCurrent.pVecPositions[i], 0.0f);
			rFrame.camera.fCameraShake = 1.0f;
		}
	}
						
	{
		SCOPED_CPU_PROFILE(engine::kCpuTimerCollisionSpaceshipsMissiles);

		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
			{
				continue;
			}

			for (int64_t j = 0; j < rFrame.missiles.iCount; ++j)
			{
				if (rFrame.missiles.pFlags[j] & MissileFlags::kTargetPlayer || rFrame.missiles.pFlags[j] & MissileFlags::kExploding)
				{
					continue;
				}

				float fDistance = common::Distance(rFrame.missiles.pVecPositions[j], rCurrent.pVecPositions[i]);
				if (fDistance > kfMissileCollisionRadius) [[likely]]
				{
					continue;
				}

				Missiles::Explode(rFrame, rFrameInput, j, false);
			}
		}
	}

	// DT: TODO This code is repeated in every enemy, create a collide pool of some sort
	{
		SCOPED_CPU_PROFILE(engine::kCpuTimerCollisionSpaceshipsBlasters);

		for (int64_t i = 0; i < rCurrent.iCount; ++i)
		{
			if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
			{
				continue;
			}

				if (engine::OutsideVisibleArea(rFrameInput, rCurrent.pVecPositions[i]))
				{
					continue;
				}

			for (int64_t j = 0; j < rFrame.blasters.iCount; ++j)
			{
				if (rFrame.blasters.pFlags[j] & kImpactObject || !(rFrame.blasters.pFlags[j] & kCollideEnemies))
				{
					continue;
				}

				float fDistance = common::Distance(rFrame.blasters.pVecPositions[j], rCurrent.pVecPositions[i]);
				if (fDistance > kfBlasterCollisionRadius) [[likely]]
				{
					continue;
				}

				engine::gpAudioManager->PlayOneShot(data::kAudioBlaster466834__mikee63__enhancedblasterwavCrc, rCurrent.pVecPositions[i], 0.16f);

				Blasters::CollisionEffect(rFrame, j);
				Frame::BlasterImpact(rFrame, j, rCurrent.pVecPositions[i]);

				float fVisibilityDamage = VisibilityToDamagePercent(rFrameInput, rCurrent.pVecPositions[i]);
				rCurrent.pfHealths[i] -= fVisibilityDamage * rFrame.blasters.pfDamages[j];
				if (rCurrent.pfHealths[i] <= 0.0f) [[unlikely]]
				{
					Explode(rFrame, i, XMVector3Normalize(rFrame.blasters.pVecVelocities[j]));
					break;
				}

				// Temporary freeze on hit
				rCurrent.pfFreezeTimes[i] = std::max(kfFreezeTimeBlaster, rCurrent.pfFreezeTimes[i]);
			}
		}
	}

	// Take area damage
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		for (decltype(rFrame.playerAreas.uiMaxIndex) j = 0; j <= rFrame.playerAreas.uiMaxIndex; ++j)
		{
			if (!rFrame.playerAreas.pbUsed[j])
			{
				continue;
			}

			engine::AreaInfo& rAreaInfo = rFrame.playerAreas.pObjectInfos[j];
			if (common::InsideAreaVertices(rCurrent.pVecPositions[i], rAreaInfo.areaVertices)) [[unlikely]]
			{
				auto vecDirection = rFrame.player.vecDirection;
				SpawnBurnParticles(rCurrent.pVecPositions[i], rCurrent.pVecVelocities[i], vecDirection, kfBurnSize, 1.0f);

				rCurrent.pfHealths[i] -= fDeltaTime * rAreaInfo.fDamagePerSecond;
				if (rCurrent.pfHealths[i] <= 0.0f) [[unlikely]]
				{
					Explode(rFrame, i, vecDirection);
					break;
				}
			}
		}
	}
}

void XM_CALLCONV Spaceships::Spawn([[maybe_unused]] Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection)
{
	Spaceships& rCurrent = rFrame.spaceships;

	if (rCurrent.iCount == kiMax)
	{
		return;
	}
	int64_t i = rCurrent.iCount++;

	rCurrent.pFlags[i].ClearAll();

	rCurrent.pVecPositions[i] = vecPosition;
	ASSERT(XMVectorGetW(rCurrent.pVecPositions[i]) == 1.0f);

	rCurrent.pVecDirections[i] = vecDirection;
	ASSERT(XMVectorGetW(rCurrent.pVecDirections[i]) == 0.0f);

	rCurrent.puiPushers[i] = 0;
	rCurrent.puiTargets[i] = 0;
	rCurrent.puiDamageTrails[i] = 0;
	rCurrent.puiBillboards[i] = 0;

	rCurrent.pVecVelocities[i] = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	ASSERT(XMVectorGetW(rCurrent.pVecVelocities[i]) == 0.0f);
	rCurrent.pfDeltaRotations[i] = 0.0f;

	rCurrent.pfHealths[i] = EnemyHealthMultiplier(rFrame) * kfSpaceshipHealth;

	rCurrent.pfFreezeTimes[i] = 0.0f;
	rCurrent.pfDestroyedTimes[i] = 0.0f;
	rCurrent.pfDestroyedExplosionTimes[i] = 0.0f;
	rCurrent.pfNextBlasterSpawnTimes[i] = 0.0f;
	rCurrent.piBlasterSpawns[i] = 2;
}

void Spaceships::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
}

void Spaceships::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Spaceships& rCurrent = rFrame.spaceships;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (!(rCurrent.pFlags[i] & kExploding) || rCurrent.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		rFrame.pushers.Remove(rCurrent.puiPushers[i]);
		rFrame.targets.Remove(rFrame, rCurrent.puiTargets[i], {kDestination});
		rFrame.trails.Remove(rCurrent.puiDamageTrails[i]);
		rFrame.billboards.Remove(rCurrent.puiBillboards[i]);

		if (rCurrent.iCount - 1 > i) [[likely]]
		{
			rCurrent.Copy(i, rCurrent.iCount - 1);
			--i;
		}
		--rCurrent.iCount;
	}
}

void Spaceships::RenderGlobal([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

void Spaceships::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
	static auto sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationY(0.0f) * XMMatrixRotationZ(XM_PIDIV2);

	const Spaceships& rCurrent = rFrame.spaceships;
	PROFILE_SET_COUNT(engine::kCpuCounterSpaceships, rCurrent.iCount);
	auto pLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mSpaceshipsStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iSpaceshipsRendered = 0;
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!engine::InVisibleArea(engine::gf4RenderVisibleArea, f4Position))
		{
			continue;
		}

		float fScale = 0.004f;
		if (rCurrent.pFlags[i] & kExploding)
		{
			fScale *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.5f);
		}

		auto matScaling = XMMatrixScaling(fScale, fScale, fScale);
		auto matRoll = XMMatrixRotationX(-kfRoll * rCurrent.pfDeltaRotations[i]);
		auto matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		auto matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);

		auto matTransform = matScaling * sMatPreRotate * matRoll * matYaw * matTranslation;

		shaders::GltfLayout& rGltfLayout = pLayouts[iSpaceshipsRendered++];
		rGltfLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		float fFreezeColor = std::clamp(rCurrent.pfFreezeTimes[i] / kfFreezeTimeBlaster, 0.0f, 1.0f);
		rGltfLayout.f4ColorAdd = {0.5f * fFreezeColor, 0.25f * fFreezeColor, 0.25f * fFreezeColor, 0.0f};
	}
	PROFILE_SET_COUNT(engine::kCpuCounterSpaceshipsRendered, iSpaceshipsRendered);

	gpGltfPipelines->mpGltfPipelines[kGltfPipelineSpaceships].WriteIndirectBuffer(iCommandBuffer, iSpaceshipsRendered);
	gpGltfPipelines->mpGltfPipelines[kGltfPipelineSpaceshipsShadow].WriteIndirectBuffer(iCommandBuffer, iSpaceshipsRendered);
}

} // namespace game
