#include "Spaceships.h"

#include "Data/Audio.h"
#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Targets/Targets.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Ui/SoundWrappers.h"
#endif

namespace game
{

using enum SpaceshipFlags;

// Ai / combat
constexpr float kfHealthRegen = 0.1f;
constexpr float kfHealthRegenDistance = 60.0f;
constexpr float kfDeathKnockbackSpeed = 20.0f;

// Collision layer index (set each frame in PreCollision)
// thread_local: parallel per-Frame tick via Dispatch
static thread_local size_t suiCollisionLayerIndex = 0;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;

struct SpaceshipCollisionIntervalScratch
{
	std::vector<float> startTimes;
	std::vector<float> endTimes;
	std::vector<float> maxTimes;
};

static SpaceshipCollisionIntervalScratch& GetSpaceshipCollisionIntervalScratch()
{
	// Function-local TLS defers construction until first use; default construction is allocation-free
	// (empty vectors), so it is safe even before allocator startup completes. Growth sites suppress tracking.
	static thread_local SpaceshipCollisionIntervalScratch sScratch;
	return sScratch;
}

// Shared type indices (defined in Spaceships.cpp, set during Register())
extern uint8_t gSpaceshipExplosionTypeIndex;
#if defined(BT_CLIENT)
extern uint8_t gSpaceshipHitFlashControllerTypeIndex;
#endif

// Forward declaration (defined in Spaceships.cpp)
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
	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], gSpaceshipDeathVolume.Get(), gSpaceshipDeathPitchMin.Get(), gSpaceshipDeathPitchRandom.Get());
#endif

	XMVECTOR vecDirection = XMVector3Normalize(rCurrentPostRender.pVecVelocities[i]);
	SpawnSpaceshipExplosion(rFrame, rCurrentInterpolate.pVecPositions[i], vecDirection, 1.0f);
}

void XM_CALLCONV SpaceshipsPostRender::RegenerateHealth(FXMVECTOR vecPosition, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, SpaceshipFlags_t flags, float fDeltaTime, float& rfHealth)
{
	if (!(flags & kExploding) && bPlayerAlive && common::Distance(vecPosition, vecNearestPlayer) > kfHealthRegenDistance) [[unlikely]]
	{
		rfHealth = std::min(rfHealth + fDeltaTime * kfHealthRegen, kfSpaceshipHealth);
	}
}

void XM_CALLCONV SpaceshipsPostRender::ApplyDeathKnockback(FXMVECTOR vecDamageDirection, XMVECTOR& rVecVelocity)
{
	rVecVelocity = XMVectorScale(XMVectorNegate(vecDamageDirection), kfDeathKnockbackSpeed);
}

void SpaceshipsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	SpaceshipCollisionIntervalScratch& rCollisionScratch = GetSpaceshipCollisionIntervalScratch();
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppress;

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
	rCollisionScratch.startTimes.resize(uiCount);
	rCollisionScratch.endTimes.resize(uiCount);
	rCollisionScratch.maxTimes.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
		sCollisionRadii.at(static_cast<size_t>(i)) = kfSpaceshipRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = kfSpaceshipCollisionDamage;
		rCollisionScratch.startTimes.at(static_cast<size_t>(i)) = 0.0f;
		rCollisionScratch.endTimes.at(static_cast<size_t>(i)) = 1.0f;
		SegmentHit boundaryHit = TracePointToFrameExit(rStaticData.vecArea, rPreviousFrame.interpolate.pSpaceships->pVecPositions[i], rCurrentInterpolate.pVecPositions[i], 0.0f, 1.0f);
		rCollisionScratch.maxTimes.at(static_cast<size_t>(i)) = boundaryHit.bHit ? boundaryHit.fTime : std::numeric_limits<float>::max();
	}

	// Add spaceship layer to Collision
	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecStartPositions = rPreviousFrame.interpolate.pSpaceships->pVecPositions,
		.pVecEndPositions = rCurrentInterpolate.pVecPositions,
		.pfStartTimes = rCollisionScratch.startTimes.data(),
		.pfEndTimes = rCollisionScratch.endTimes.data(),
		.pfMaxTimes = rCollisionScratch.maxTimes.data(),
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.pVecVelocities = rCurrentPostRender.pVecVelocities,
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kSpaceship,
		.uiCollidesWith = CollidesWith::kSpaceship,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void SpaceshipsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

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
					engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster793907__cvltiv8r__snaresbycvltiv8r301wavCrc, rCurrentInterpolate.pVecPositions[i], gSpaceshipHitVolume.Get());
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

		if (!(rCurrentPostRender.pFlags[i] & kExploding) && IsOutOfBounds(bounds, rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
		{
			// Entity candidates at or beyond frame exit were filtered during PreCollision.
			rCurrentPostRender.pFlags[i].Set(kTransfer);
		}
	}
}

void SpaceshipsPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
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

} // namespace game
