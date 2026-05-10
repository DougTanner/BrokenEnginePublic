#include "Spaceships.h"

#include "Data/Audio.h"
#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
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

// Shared type indices (defined in Spaceships.cpp, set during Register())
extern uint8_t gSpaceshipExplosionTypeIndex;
extern uint8_t gSpaceshipTargetTypeIndex;
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
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
		sCollisionRadii.at(static_cast<size_t>(i)) = kfSpaceshipRadius;
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
