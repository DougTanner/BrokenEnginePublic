#include "Spaceships.h"

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Pushers/Pushers.h"

#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Frame/Collections/Players/Players.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Data/Scene.h"
#endif

#include "Data/Audio.h"

namespace game
{

using enum SpaceshipFlags;

// Collision layer index (set each frame in PreCollision)
// thread_local: parallel per-Frame tick via Dispatch
static thread_local size_t suiCollisionLayerIndex = 0;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;

// Shared type indices (defined in Spaceships.cpp, set during Register())
extern uint8_t gSpaceshipExplosionTypeIndex;
extern uint8_t gSpaceshipTargetTypeIndex;
#if defined(BT_CLIENT)
extern uint8_t gSpaceshipHitFlashControllerTypeIndex;
#endif

// Spaceship Ai
constexpr float kfHealthRegen = 0.1f;
constexpr float kfVelocityDecay = 0.25f;
constexpr float kfAccelerationTowardsPlayer = 4.0f;
constexpr float kfFleePlayerAcceleration = 6.0f;
constexpr float kfReturnToIslandCenterAcceleration = 10.0f;
constexpr float kfDeltaAngleChange = 0.999f;
constexpr float kfDeltaAngleDecay = 6.0f;
constexpr float kfDeltaAngleTowardsPlayer = 32.0f;
constexpr float kfFleePlayerDeltaAngle = 32.0f;
constexpr float kfFleePlayerStart = 15.0f;
constexpr float kfFleePlayerEnd = 25.0f;
constexpr float kfReturnDistance = 180.0f;
constexpr float kfReturnedDistance = kfReturnDistance - 20.0f;
constexpr float kfDeltaAngleMax = 4.0f;
constexpr float kfVelocityToDirection = 4.0f;
constexpr float kfDeathKnockbackSpeed = 20.0f;
constexpr float kfTerrainCollisionRotation = 8.0f;
constexpr float kfTerrainCollisionMovePosition = 4.0f;
constexpr float kfTerrainCollisionAddVelocity = 4.0f;

// Spaceship collision
constexpr float kfSpaceshipCollisionRadius = 2.0f;
constexpr float kfHealthRegenDistance = 60.0f;

// Terrain avoidance (player-proximity skip constants; sampling constants in GameUtils.cpp)
constexpr float kfIgnoreAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfIgnoreAvoidTerrainPlayerDistance = 40.0f;

#if defined(BT_CLIENT)
// Spaceship model (defined in SpaceshipsRender.cpp)
extern const common::crc_t kSpaceshipModel;
#endif

// Forward declarations for shared helpers (defined in Spaceships.cpp)
[[nodiscard]] bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult);
void XM_CALLCONV SyncSpaceship(FrameInterpolate& rFrameInterpolate, engine::pusher_t uiPusher, target_t uiTarget, FXMVECTOR vecPosition);
void SpawnSpaceshipExplosion(Frame& __restrict rFrame, XMVECTOR vecPosition, XMVECTOR vecDirection, float fPercent);

static void XM_CALLCONV BeginExplosion(Frame& rFrame, int64_t i, FXMVECTOR vecDamageDirection)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	rCurrentPostRender.pFlags[i].Set(kExploding);
	rCurrentInterpolate.pfDestroyedTimes[i] = kfSpaceshipDestroyTime;
	rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfSpaceshipDestroyExplosionInterval;

	// Store damage direction for knockback
	rCurrentPostRender.pVecDamageDirections[i] = vecDamageDirection;

	// Remove target so missiles stop tracking
	TargetsPostRender::Remove(rFrame, rCurrentInterpolate.puiTargets[i], {TargetFlags::kDestination});
	rCurrentInterpolate.puiTargets[i] = {};

	// Play explosion audio
#if defined(BT_CLIENT)
	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], kfSpaceshipDeathExplosionVolume, kfSpaceshipDeathPitchMin, kfSpaceshipDeathPitchRandom);
#endif

	XMVECTOR vecDirection = XMVector3Normalize(rCurrentPostRender.pVecVelocities[i]);
	SpawnSpaceshipExplosion(rFrame, rCurrentInterpolate.pVecPositions[i], vecDirection, 1.0f);
}

void SpaceshipsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateUpdateSpaceships);

	SpaceshipsInterpolate& rCurrent = *rCurrentFrameInterpolate.pSpaceships;
	const SpaceshipsInterpolate& rPrevious = *rPreviousFrame.interpolate.pSpaceships;
	const SpaceshipsPostRender& rPreviousPostRender = *rPreviousFrame.postRender.pSpaceships;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	// Hoist animation duration lookup outside the loop
#if defined(BT_CLIENT)
	float fAnimationDuration = 0.0f;
	if (engine::gAnimationDataMap.contains(kSpaceshipModel))
	{
		fAnimationDuration = engine::gAnimationDataMap.at(kSpaceshipModel).mpAnimations[0].fDuration;
	}
#endif

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fFreezeTime = rPrevious.pfFreezeTimes[i];
#if defined(BT_CLIENT)
		float fAnimationTime = rPrevious.pfAnimationTimes[i];
#endif

		// Add velocity to position (unless frozen)
		if (fFreezeTime <= 0.0f)
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], vecPosition);
		}

		// Add delta rotation to direction
		vecDirection = XMVector3Normalize(XMVector4Transform(vecDirection, XMMatrixRotationZ(fDeltaTime * fDeltaRotation)));

		// Decay destroyed time (only when exploding, i.e., > 0.0f; sentinel -1.0f stays unchanged)
		if (fDestroyedTime > 0.0f)
		{
			fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
		}

		// Advance animation time
#if defined(BT_CLIENT)
		if (fAnimationDuration > 0.0f)
		{
			fAnimationTime += fDeltaTime;
			if (fAnimationTime >= fAnimationDuration)
			{
				fAnimationTime = std::fmod(fAnimationTime, fAnimationDuration);
			}
		}
#endif // BT_CLIENT

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfFreezeTimes[i] = fFreezeTime;
#if defined(BT_CLIENT)
		rCurrent.pfAnimationTimes[i] = fAnimationTime;
#endif

		// Sync owned objects (IDs copied in AllocateAndCopy)
		SyncSpaceship(rCurrentFrameInterpolate, rCurrent.puiPushers[i], rCurrent.puiTargets[i], vecPosition);

		// Sync wind deposit
#if defined(BT_CLIENT)
		if (rCurrent.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsInterpolate::Sync(rCurrentFrameInterpolate, rCurrent.puiWindTrails[i],
			{
				.vecPosition = vecPosition,
				.fIntensity = game::gWindDepositSpaceshipsIntensity.Get(),
				.fWidth = game::gWindDepositSpaceshipsWidth.Get(),
				.fLengthMultiplier = game::gWindDepositSpaceshipsLengthMultiplier.Get(),
			});
		}
#endif // BT_CLIENT
	}
}

void SpaceshipsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateSpaceships);

	SpaceshipsPostRender& __restrict rCurrent = *rFrame.postRender.pSpaceships;
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	const SpaceshipsPostRender& rPrevious = *rPreviousFrame.postRender.pSpaceships;
	const SpaceshipsInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pSpaceships;
	const PlayersInterpolate& rPlayers = *rPreviousFrame.interpolate.pPlayers;
	const PlayersPostRender& rPlayersPostRender = *rPreviousFrame.postRender.pPlayers;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load from PostRender (static fields copied via memcpy in AllocateAndCopy)
		SpaceshipFlags_t flags = rPrevious.pFlags[i];
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
		float fHealth = rPrevious.pfHealths[i];
		float fDestroyedExplosionTime = rPrevious.pfDestroyedExplosionTimes[i] - fDeltaTime;
		float fNextBlasterSpawnTime = rPrevious.pfNextBlasterSpawnTimes[i] - fDeltaTime;

		// Load from Interpolate (these are now in Interpolate)
		float fDeltaRotation = rPreviousInterpolate.pfDeltaRotations[i];
		float fFreezeTime = rPreviousInterpolate.pfFreezeTimes[i] - fDeltaTime;

		// Find nearest alive player for this spaceship
		XMVECTOR vecNearestPlayer = XMVectorZero();
		bool bPlayerAlive = NearestAlivePlayerPosition(rPlayers, rPlayersPostRender, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer);

		if (!(flags & kExploding) && bPlayerAlive && common::Distance(rCurrentInterpolate.pVecPositions[i], vecNearestPlayer) > kfHealthRegenDistance) [[unlikely]]
		{
			fHealth = std::min(fHealth + fDeltaTime * kfHealthRegen, kfSpaceshipHealth);
		}

		XMVECTOR vecToPlayer = bPlayerAlive ? XMVectorSubtract(vecNearestPlayer, rCurrentInterpolate.pVecPositions[i]) : XMVectorZero();
		float fPlayerDistance = bPlayerAlive ? XMVectorGetX(XMVector3Length(vecToPlayer)) : kfFleePlayerEnd + 1.0f;
		if (fPlayerDistance < kfFleePlayerStart)
		{
			flags.Set(kFleePlayer);
		}
		else if (fPlayerDistance > kfFleePlayerEnd)
		{
			flags.Clear(kFleePlayer);
		}

		XMVECTOR vecIslandCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		float fDistanceFromIslandCenter = common::Distance(rCurrentInterpolate.pVecPositions[i], vecIslandCenter);
		if (fDistanceFromIslandCenter > kfReturnDistance)
		{
			flags.Set(kReturnToIslandCenter);
		}
		else if (fDistanceFromIslandCenter < kfReturnedDistance)
		{
			flags.Clear(kReturnToIslandCenter);
		}

		XMVECTOR vecDestination = bPlayerAlive ? vecNearestPlayer : vecIslandCenter;
		if (flags & kReturnToIslandCenter)
		{
			vecDestination = vecIslandCenter;
		}
		XMVECTOR vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(vecDestination, rCurrentInterpolate.pVecPositions[i]));
		float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecToDestinationNormal));
		float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaAngleTowardsPlayer : -kfDeltaAngleTowardsPlayer;
		if (!(flags & kReturnToIslandCenter) && flags & kFleePlayer)
		{
			fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? -kfFleePlayerDeltaAngle : kfFleePlayerDeltaAngle;
		}

		fDeltaRotation = kfDeltaAngleChange * fDeltaRotation + (1.0f - kfDeltaAngleChange) * fWantedDeltaRotation;
		fDeltaRotation = common::ExponentialDecay(kfDeltaAngleDecay, fDeltaTime) * fDeltaRotation;

		if (!(flags & kExploding)) [[likely]]
		{
			// Decay velocity
			vecVelocity = XMVectorMultiply(XMVectorReplicate(common::ExponentialDecay(kfVelocityDecay, fDeltaTime)), vecVelocity);

			// Accelerate and rotate velocity
			float fAcceleration = flags & kReturnToIslandCenter ? kfReturnToIslandCenterAcceleration : flags & kFleePlayer ? kfFleePlayerAcceleration : kfAccelerationTowardsPlayer;
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAcceleration), rCurrentInterpolate.pVecDirections[i], vecVelocity);

			// Blend velocity direction toward facing direction
			float fPercent = 1.0f - fDeltaTime * kfVelocityToDirection;
			XMVECTOR vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fPercent), XMVector3Normalize(vecVelocity));
			XMVECTOR vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fPercent), rCurrentInterpolate.pVecDirections[i]);
			vecVelocity = XMVectorMultiply(XMVector3Length(vecVelocity), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));

			// Apply push from nearby pushers (pass own pusher ID to ignore self-push)
			XMVECTOR vecPush = engine::PushersInterpolate::ApplyPush(rFrame.interpolate, rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.puiPushers[i]);
			vecVelocity = XMVectorAdd(vecVelocity, XMVectorScale(vecPush, fDeltaTime));
		}
		else
		{
			// When exploding: fixed velocity away from damage source (opposite of damage direction)
			vecVelocity = XMVectorScale(XMVectorNegate(rPrevious.pVecDamageDirections[i]), kfDeathKnockbackSpeed);
		}

		// Terrain collision bounce
		float fTerrainElevation = engine::gpIslandTerrain->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
		if (fTerrainElevation >= XMVectorGetZ(rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
		{
			XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->GlobalNormal(rCurrentInterpolate.pVecPositions[i]), 0.0f));

			rCurrentInterpolate.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionMovePosition), vecTerrainNormal, rCurrentInterpolate.pVecPositions[i]);

			float fDirectionTerrainCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecTerrainNormal));
			fDeltaRotation = fDirectionTerrainCrossZ > 0.0f ? kfTerrainCollisionRotation : -kfTerrainCollisionRotation;

			vecVelocity = XMVector3Reflect(vecVelocity, vecTerrainNormal);
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionAddVelocity), vecTerrainNormal, vecVelocity);
		}

		// Clamp delta rotation
		fDeltaRotation = common::MinAbs(fDeltaRotation, kfDeltaAngleMax);

		// Save to PostRender (static fields copied via memcpy in AllocateAndCopy)
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pfHealths[i] = fHealth;
		rCurrent.pfDestroyedExplosionTimes[i] = fDestroyedExplosionTime;
		rCurrent.pfNextBlasterSpawnTimes[i] = fNextBlasterSpawnTime;

		// Save to Interpolate (these are now in Interpolate)
		rCurrentInterpolate.pfDeltaRotations[i] = fDeltaRotation;
		rCurrentInterpolate.pfFreezeTimes[i] = fFreezeTime;

	}

	SpaceshipsPostRender::AvoidTerrain(rFrame, rPreviousFrame, 0, rFrame.interpolate.pSpaceships->iCount);
}

void SpaceshipsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	// Build collision arrays
	size_t uiCount = static_cast<size_t>(rCurrentInterpolate.iCount);
	sCollisionFlags.resize(uiCount);
	sCollisionRadii.resize(uiCount);
	sCollisionDamages.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
		sCollisionRadii.at(static_cast<size_t>(i)) = kfSpaceshipCollisionRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = kfSpaceshipCollisionDamage;
	}

	// Add spaceship layer to Collision
	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kSpaceship,
		.uiCollidesWith = CollidesWith::kSpaceship,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void SpaceshipsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Flag for transfer if outside frame boundaries (Transfer phase handles removal)
		if (IsOutOfBounds(bounds, vecPosition)) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kTransfer);
			continue;
		}

		// Check collision results - spaceships take damage from player blasters only
		// Note: Missile damage is handled via area damage system in AreaDamage phase
		if (engine::Collision::HasCollision(suiCollisionLayerIndex, i))
		{
			std::span<const engine::CollisionResult> collisions = engine::Collision::GetCollisions(suiCollisionLayerIndex, i);
			for (const engine::CollisionResult& rResult : collisions)
			{
				if (rResult.uiOtherCategory == CollisionCategory::kBlaster)
				{
					rCurrentPostRender.pfHealths[i] -= rResult.fDamageReceived;

					// Play hit sound
#if defined(BT_CLIENT)
					engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster793907__cvltiv8r__snaresbycvltiv8r301wavCrc, rCurrentInterpolate.pVecPositions[i], kfSpaceshipHitSoundVolume);
#endif

					// Spawn hit flash effect at collision point
#if defined(BT_CLIENT)
					engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, gSpaceshipHitFlashControllerTypeIndex, rResult.vecContactPoint, 0.0f);
#endif

					if (rCurrentPostRender.pfHealths[i] <= 0.0f)
					{
						XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorNegate(rResult.vecOtherVelocity));
						BeginExplosion(rFrame, i, vecDamageDirection);
						break;
					}
				}
			}
		}
	}
}

void SpaceshipsPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Skip already exploding or transferring spaceships
		if ((rCurrentPostRender.pFlags[i] & kExploding) || (rCurrentPostRender.pFlags[i] & kTransfer))
		{
			continue;
		}

		// Query area damage from missiles (filter by kMissile category)
		XMVECTOR vecClosestSource {};
		float fDamage = engine::AreaDamage::Get(rCurrentInterpolate.pVecPositions[i], CollisionCategory::kMissile, vecClosestSource);

		if (fDamage <= 0.0f)
		{
			continue;
		}

		// Apply damage
		rCurrentPostRender.pfHealths[i] -= fDamage;

		if (rCurrentPostRender.pfHealths[i] <= 0.0f)
		{
			XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorSubtract(vecClosestSource, rCurrentInterpolate.pVecPositions[i]));
			BeginExplosion(rFrame, i, vecDamageDirection);
		}
	}
}

void SpaceshipsPostRender::AvoidTerrain([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, int64_t iStart, int64_t iEnd)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		// Skip terrain avoidance if close to nearest alive player and facing them
		XMVECTOR vecNearestPlayer = XMVectorZero();
		if (NearestAlivePlayerPosition(*rFrame.interpolate.pPlayers, *rFrame.postRender.pPlayers, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer))
		{
			XMVECTOR vecToPlayer = XMVectorSubtract(vecNearestPlayer, rCurrentInterpolate.pVecPositions[i]);
			float fDistanceToPlayer = XMVectorGetX(XMVector3Length(vecToPlayer));
			if (fDistanceToPlayer < kfIgnoreAvoidTerrainPlayerDistance)
			{
				XMVECTOR vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
				float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrentInterpolate.pVecDirections[i], vecToPlayerNormal));
				if (fAngleToPlayer < kfIgnoreAvoidTerrainPlayerAngle)
				{
					continue;
				}
			}
		}

		rCurrentInterpolate.pfDeltaRotations[i] = ComputeTerrainAvoidance(
			rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.pVecDirections[i],
			rCurrentInterpolate.pfDeltaRotations[i]);

		// Clamp delta rotation
		rCurrentInterpolate.pfDeltaRotations[i] = common::MinAbs(rCurrentInterpolate.pfDeltaRotations[i], kfDeltaAngleMax);
	}
}

} // namespace game
