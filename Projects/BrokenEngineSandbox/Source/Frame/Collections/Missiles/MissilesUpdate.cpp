#include "Missiles.h"

#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Targets/Targets.h"

#include "Data/Audio.h"

namespace game
{

using enum MissileFlags;

// Collision layer index (set each frame in PreCollision)
// thread_local: parallel per-Frame tick via Dispatch
static thread_local size_t suiCollisionLayerIndex = 0;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;

// Missile AI
constexpr float kfAccelerationAtMaxDeltaAngle = 0.9f;
constexpr float kfVelocityDecay = 1.0f;
constexpr float kfVelocityToDirection = 16.0f;
constexpr float kfJitterIntervalRandom = 0.0025f;
constexpr float kfDirectionJitterRandom = 0.06f;
constexpr float kfDeltaAngleJitterRandom = 0.5f;
constexpr float kfDeltaAngleJitterRandomWithTarget = 1.0f;
constexpr float kfDeltaRotationChange = 0.925f;
constexpr float kfDeltaRotationDecay = 8.0f;
constexpr float kfDeltaRotationTowardsTarget = 10.0f;
constexpr float kfDeltaRotationTowardsStored = 3.0f;

#if defined(BT_CLIENT)
// Forward declaration of SyncMissile (defined in Missiles.cpp, also used by ClientInit)
void XM_CALLCONV SyncMissile(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight, engine::pusher_t uiPusher, engine::smoke_trails_t uiSmokeTrail,
	engine::sound_t uiSound,
	FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, GXMVECTOR vecPreviousPosition, MissileFlags_t flags, float fPitch, float fDeltaRotation, float fExhaustLength);
#endif

void MissilesInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	MissilesInterpolate& rCurrent = *rCurrentFrameInterpolate.pMissiles;
	const MissilesInterpolate& rPrevious = *rPreviousFrame.interpolate.pMissiles;
	const MissilesPostRender& rPreviousPostRender = *rPreviousFrame.postRender.pMissiles;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPreviousPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecPosition = vecPreviousPosition;
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		MissileFlags_t flags = rPreviousPostRender.pFlags[i];

		if (!(flags & kExploding)) [[likely]]
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], vecPosition);

			// Add delta rotation to direction (delay percentage already applied in PostRender::Update)
			vecDirection = XMVector3Normalize(XMVector4Transform(vecDirection, XMMatrixRotationZ(fDeltaTime * rPreviousPostRender.pfDeltaRotations[i])));
		}

		// Decay destroyed time
		if (fDestroyedTime > 0.0f)
		{
			fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;

		// Sync owned objects (IDs copied in AllocateAndCopy)
#if defined(BT_CLIENT)
		SyncMissile(rCurrentFrameInterpolate, rCurrent.puiAreaLights[i], rCurrent.puiPushers[i], rCurrent.puiSmokeTrails[i],
			rPreviousPostRender.puiSounds[i],
			vecPosition, vecDirection, rPreviousPostRender.pVecVelocities[i], vecPreviousPosition, flags, rPreviousPostRender.pfPitches[i], rPreviousPostRender.pfDeltaRotations[i], rPreviousPostRender.pfExhaustLengths[i]);
#else
		engine::PushersInterpolate::Sync(rCurrentFrameInterpolate, rCurrent.puiPushers[i],
		{
			.vecPosition = vecPosition,
			.fRadius = kfMissilePusherRadius,
			.fIntensity = kfMissilePusherIntensity,
			.fPower = kfMissilePusherPower,
			.flags = {engine::PusherFlags::kTypeDefault},
		});
#endif // BT_CLIENT
	}
}

void MissilesPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	MissilesPostRender& __restrict rCurrent = *rFrame.postRender.pMissiles;
	const MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	const MissilesPostRender& rPrevious = *rPreviousFrame.postRender.pMissiles;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load dynamic fields (static fields copied via memcpy in AllocateAndCopy)
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
		target_t uiTarget = rPrevious.puiTargets[i];
		float fTime = rPrevious.pfTimes[i] + fDeltaTime;
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fDeltaRotationDelay = rPrevious.pfDeltaRotationDelays[i];
		float fExaustDelay = rPrevious.pfExaustDelays[i] - fDeltaTime;
		float fExhaustLength = rPrevious.pfExhaustLengths[i];
		float fNextJitter = rPrevious.pfNextJitter[i] - fDeltaTime;
		XMVECTOR vecStoredDirection = rPrevious.pVecStoredDirections[i];

		if (!(rCurrent.pFlags[i] & kExploding)) [[likely]]
		{
			// Decay velocity
			vecVelocity = XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfVelocityDecay), vecVelocity);

			// Accelerate
			float fDeltaAnglePercent = std::abs(fDeltaRotation) / rCurrent.pfDeltaRotationMax[i];
			fDeltaAnglePercent = std::clamp(fDeltaAnglePercent, 0.0f, 1.0f);
			float fAdjustedAcceleration = (1.0f - fDeltaAnglePercent) * rCurrent.pfAccelerations[i] + fDeltaAnglePercent * kfAccelerationAtMaxDeltaAngle * rCurrent.pfAccelerations[i];
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAdjustedAcceleration), rCurrentInterpolate.pVecDirections[i], vecVelocity);

			// Rotate velocity towards direction
			float fVelocityToDirectionPercent = 1.0f - fDeltaTime * kfVelocityToDirection;
			XMVECTOR vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fVelocityToDirectionPercent), XMVector3Normalize(vecVelocity));
			XMVECTOR vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fVelocityToDirectionPercent), rCurrentInterpolate.pVecDirections[i]);
			vecVelocity = XMVectorMultiply(XMVector3Length(vecVelocity), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));

			// Decay delta rotation
			fDeltaRotation = (1.0f - fDeltaTime * kfDeltaRotationDecay) * fDeltaRotation;

			// Jitter direction
			if (fNextJitter < 0.0f)
			{
				fNextJitter = common::Random<kfJitterIntervalRandom>(rFrame.postRender.randomEngine);

				uint32_t uiRandom = common::Random(2u, rFrame.postRender.randomEngine);
				float fDeltaAnglePercentExtra = 1.0f + 3.0f * fDeltaAnglePercent;
				float fDeltaAngleJitter = !uiTarget.IsValid() ? kfDeltaAngleJitterRandom : kfDeltaAngleJitterRandomWithTarget;
				if (uiRandom == 0) { vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fDeltaAnglePercentExtra * (-kfDirectionJitterRandom + common::Random(2.0f * kfDirectionJitterRandom, rFrame.postRender.randomEngine)))); }
				if (uiRandom == 1) { fDeltaRotation += fDeltaAnglePercentExtra * (-fDeltaAngleJitter + fDeltaAngleJitter * common::Random<2.0f>(rFrame.postRender.randomEngine)); }
			}

			// Generate new random exhaust length every frame
			fExhaustLength = kfMissileExhaustLength + common::Random<kfMissileExhaustLengthRandom>(rFrame.postRender.randomEngine);

			// Track target or orient toward stored direction
			if (uiTarget.IsValid())
			{
				const TargetsInterpolate& rTargets = *rPreviousFrame.interpolate.pTargets;

				// Check if target was force-removed (spaceship died)
				if (!rTargets.idToIndexMap.contains(uiTarget))
				{
					// Target no longer exists - clear our reference
					uiTarget = {};
					vecStoredDirection = rCurrentInterpolate.pVecDirections[i];
				}
				else
				{
					const TargetsPostRender& rTargetsPostRender = *rPreviousFrame.postRender.pTargets;
					int64_t iTargetIndex = rTargets.IdToIndex(uiTarget);

					// Check if target still has a destination (spaceship owner)
					if (!(rTargetsPostRender.pFlags[iTargetIndex] & TargetFlags::kDestination))
					{
						// Target's source was destroyed - release subscription and capture current direction
						TargetsPostRender::Remove(rFrame, uiTarget, {});
						uiTarget = {};
						vecStoredDirection = rCurrentInterpolate.pVecDirections[i];
					}
					else
					{
						fDeltaRotationDelay -= fDeltaTime;

						XMVECTOR vecTargetPosition = rTargets.pVecPositions[iTargetIndex];
						XMVECTOR vecToTargetNormal = XMVector3Normalize(XMVectorSubtract(vecTargetPosition, rCurrentInterpolate.pVecPositions[i]));
						float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecToTargetNormal));
						float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaRotationTowardsTarget : -kfDeltaRotationTowardsTarget;

						// Apply delay percentage so rotation ramps up gradually
						float fDelayPercent = std::clamp(1.0f - fDeltaRotationDelay / kfMissileDeltaRotationDelay, 0.0f, 1.0f);
						fWantedDeltaRotation *= fDelayPercent;

						fDeltaRotation = kfDeltaRotationChange * fDeltaRotation + (1.0f - kfDeltaRotationChange) * fWantedDeltaRotation;
					}
				}
			}
			else
			{
				fDeltaRotationDelay -= fDeltaTime;

				// For untargeted missiles, gradually orient toward stored direction
				XMVECTOR vecCurrentDirection = rCurrentInterpolate.pVecDirections[i];
				float fDirectionCrossZ = XMVectorGetZ(XMVector3Cross(vecCurrentDirection, vecStoredDirection));
				float fWantedDeltaRotation = fDirectionCrossZ > 0.0f ? kfDeltaRotationTowardsStored : -kfDeltaRotationTowardsStored;

				// Apply delay percentage so rotation ramps up gradually
				float fDelayPercent = std::clamp(1.0f - fDeltaRotationDelay / kfMissileDeltaRotationDelay, 0.0f, 1.0f);
				fWantedDeltaRotation *= fDelayPercent;

				fDeltaRotation = kfDeltaRotationChange * fDeltaRotation + (1.0f - kfDeltaRotationChange) * fWantedDeltaRotation;
			}

			// Clamp delta rotation
			fDeltaRotation = common::MinAbs(fDeltaRotation, rCurrent.pfDeltaRotationMax[i]);

			// Keep velocity in XY plane
			vecVelocity = XMVectorSetZ(vecVelocity, 0.0f);
		}
		else
		{
			// Missile is exploding - single big explosion spawned in Explode()
		}

		// Save dynamic fields (static fields copied via memcpy in AllocateAndCopy)
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pVecStoredDirections[i] = vecStoredDirection;
		rCurrent.puiTargets[i] = uiTarget;
		rCurrent.pfTimes[i] = fTime;
		rCurrent.pfDeltaRotationDelays[i] = fDeltaRotationDelay;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfExaustDelays[i] = fExaustDelay;
		rCurrent.pfExhaustLengths[i] = fExhaustLength;
		rCurrent.pfNextJitter[i] = fNextJitter;
	}
}

void MissilesPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

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
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {engine::CollisionFlags::kDestroyOnCollide};
		sCollisionRadii.at(static_cast<size_t>(i)) = kfMissileCollisionRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = 0.0f;  // Damage via area damage system
	}

	// Note: Damage is applied via area damage system, not direct collision
	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.pVecVelocities = rCurrentPostRender.pVecVelocities,
		.iCount = rCurrentInterpolate.iCount,
		.bSweptTest = true,
		.uiCategory = CollisionCategory::kMissile,
		.uiCollidesWith = CollidesWith::kMissile,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void MissilesPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

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

		// Check collision results - missiles explode on hit
		if (engine::Collision::HasCollision(suiCollisionLayerIndex, i))
		{
			Explode(rFrame, i, false);
			continue;
		}

		// Collide terrain
		float fElevationFinal = engine::gpIslandTerrain->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
		if (XMVectorGetZ(rCurrentInterpolate.pVecPositions[i]) <= fElevationFinal)
		{
			Explode(rFrame, i, true);
		}
	}
}

void MissilesPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

} // namespace game
