#include "Players.h"

#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#endif

#include "Data/Audio.h"

namespace game
{

using enum PlayerFlags;

// Damage response
constexpr float kfShieldHitSoundVolumeBase = 0.1f;
constexpr float kfShieldHitSoundVolumeScale = 0.1f;
constexpr float kfShieldCooldown = 2.0f;
constexpr float kfShieldDownSoundCooldown = 2.0f;
constexpr float kfShieldDownSoundVolume = 0.1f;
constexpr float kfArmorHitSoundDamageThreshold = 3.0f;
constexpr float kfArmorHitSoundVolumeBase = 0.2f;
constexpr float kfArmorHitSoundVolumeScale = 0.5f;

void PlayersPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

// Player collision arrays
// thread_local: parallel per-Frame tick via Dispatch
static thread_local std::vector<float> sCollisionRadii;
static thread_local std::vector<float> sCollisionDamages;
static thread_local std::vector<engine::CollisionFlags_t> sCollisionFlags;

void PlayersPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	// Build collision arrays
	size_t uiCount = static_cast<size_t>(rCurrentInterpolate.iCount);
	sCollisionRadii.resize(uiCount);
	sCollisionDamages.resize(uiCount);
	sCollisionFlags.resize(uiCount);
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionRadii.at(static_cast<size_t>(i)) = kfPlayerRadius;
		sCollisionDamages.at(static_cast<size_t>(i)) = 0.0f; // Player doesn't deal collision damage
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
	}

	// Add player layer to CollisionSystem
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kPlayer,
		.uiCollidesWith = CollidesWith::kPlayer,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
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

} // namespace game
