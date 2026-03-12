#include "Spaceships.h"

#include "Ui/WrapperBase.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Pushers/Pushers.h"

#include "Frame/HealthDamage.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Frame/Collections/Players/Players.h"

#include "Data/Texture.h"

#if defined(BT_CLIENT)
#include "Frame/Collections/PointLights/PointLights.h"
#endif

#include "Data/Audio.h"

namespace engine
{
template struct Collection<game::SpaceshipsInterpolate>;
template struct Collection<game::SpaceshipsPostRender>;
}

namespace game
{

using enum SpaceshipFlags;

// Forward declarations for registration functions (called from Register())
static void RegisterEnemyBlasterType();
static void RegisterSpaceshipTargetType();

// Shared type indices (accessible from SpaceshipsUpdate.cpp via extern)
uint8_t gSpaceshipExplosionTypeIndex = 0xFF;
uint8_t gSpaceshipTargetTypeIndex = 0xFF;
#if defined(BT_CLIENT)
static uint8_t suiSpaceshipHitFlashTypeIndex = 255;
uint8_t gSpaceshipHitFlashControllerTypeIndex = 255;

static void RegisterSpaceshipHitFlashEffect();
#endif

constexpr uint32_t kuiSpaceshipExplosionBaseParticleCount = 16;
constexpr uint32_t kuiSpaceshipExplosionParticleColor = 0xFF0000FF;
constexpr float kfSpaceshipExplosionParticleVelocityMin = 1.0f;
constexpr float kfSpaceshipExplosionParticleVelocityRandom = 9.0f;
constexpr float kfSpaceshipExplosionParticleVerticalVelocityMin = -5.0f;
constexpr float kfSpaceshipExplosionParticleVerticalVelocityRandom = 10.0f;
constexpr float kfSpaceshipExplosionParticleIntensityDecay = 2.4f;
constexpr float kfSpaceshipExplosionParticleLightingSize = 3.0f;
constexpr float kfSpaceshipExplosionParticleLightingIntensity = 5000.0f;
constexpr float kfSpaceshipExplosionTrailLengthRandom = 2.5f;
constexpr uint32_t kuiSpaceshipExplosionSecondaryCount = 1;

// Enemy blaster
constexpr float kfSpawnBlasterPlayerAngle = 0.1f;
constexpr float kfBlastersSpeed = 50.0f;
constexpr float kfBlastersSpawnCooldown = 1.0f;

constexpr float kfEnemyBlasterSize = 0.3f;
constexpr float kfEnemyBlasterVisibleIntensity = 1.0f;
constexpr float kfEnemyBlasterLightingSize = 1.5f;
constexpr float kfEnemyBlasterLightingIntensity = 800.0f;

// Spaceship target
constexpr float kfTargetSize = 0.06f;
constexpr float kfTargetAlpha = 1.5f;

#if defined(BT_CLIENT)
// Hit flash effect
constexpr float kfHitFlashDuration = 0.3f;
constexpr float kfHitFlashVisibleArea = 0.5f;
constexpr float kfHitFlashVisibleIntensity = 1.0f;
constexpr float kfHitFlashLightingArea = 1.0f;
constexpr float kfHitFlashLightingIntensity = 30.0f;
#endif // BT_CLIENT


// Find the nearest alive (non-exploding) player position. Returns false if no alive players exist.
[[nodiscard]] bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult)
{
	float fClosestDistanceSq = std::numeric_limits<float>::max();
	bool bFound = false;

	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		if (rPlayersPostRender.pFlags[i] & PlayerFlags::kExploding)
		{
			continue;
		}

		float fDistanceSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(rPlayers.pVecPositions[i], vecFrom)));
		if (fDistanceSq < fClosestDistanceSq)
		{
			fClosestDistanceSq = fDistanceSq;
			rVecResult = rPlayers.pVecPositions[i];
			bFound = true;
		}
	}

	return bFound;
}

void SpaceshipsInterpolate::Register()
{
	// Spaceship explosion type
	engine::ExplosionsInterpolate::RegisterType(gSpaceshipExplosionTypeIndex,
	{
#if defined(BT_CLIENT)
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
#endif // BT_CLIENT
		.uiBaseParticleCount = kuiSpaceshipExplosionBaseParticleCount,
		.uiParticleColor = kuiSpaceshipExplosionParticleColor,
		.fParticleVelocityMin = kfSpaceshipExplosionParticleVelocityMin,
		.fParticleVelocityRandom = kfSpaceshipExplosionParticleVelocityRandom,
		.fParticleVerticalVelocityMin = kfSpaceshipExplosionParticleVerticalVelocityMin,
		.fParticleVerticalVelocityRandom = kfSpaceshipExplosionParticleVerticalVelocityRandom,
		.fParticleIntensityDecay = kfSpaceshipExplosionParticleIntensityDecay,
		.fParticleLightingSize = kfSpaceshipExplosionParticleLightingSize,
		.fParticleLightingIntensity = kfSpaceshipExplosionParticleLightingIntensity,
		.fTrailLengthRandom = kfSpaceshipExplosionTrailLengthRandom,
		.uiSecondaryExplosionCount = kuiSpaceshipExplosionSecondaryCount,
	});

	RegisterSpaceshipTargetType();
	RegisterEnemyBlasterType();
#if defined(BT_CLIENT)
	RegisterSpaceshipHitFlashEffect();
#endif
}

// Enemy blaster type registration
static uint8_t suiEnemyBlasterAreaLightTypeIndex = 0xFF;
static uint8_t suiEnemyBlasterTypeIndex = 0xFF;

static void RegisterEnemyBlasterType()
{
	if (suiEnemyBlasterTypeIndex != 0xFF)
	{
		return;
	}

#if defined(BT_CLIENT)
	// Register area light type for enemy blasters
	engine::AreaLightsInterpolate::RegisterType(suiEnemyBlasterAreaLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC72pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = kfEnemyBlasterVisibleIntensity,
		.fLightingSize = kfEnemyBlasterLightingSize,
		.fLightingIntensity = kfEnemyBlasterLightingIntensity,
	});
#endif // BT_CLIENT

	// Register blaster type with area light
	BlastersInterpolate::RegisterType(suiEnemyBlasterTypeIndex,
	{
		.f2Size = {kfEnemyBlasterSize, kfEnemyBlasterSize},
		.uiAreaLightTypeIndex = suiEnemyBlasterAreaLightTypeIndex,
	});
}

// Helper to sync owned objects for a spaceship
void XM_CALLCONV SyncSpaceship(FrameInterpolate& rFrameInterpolate, engine::pusher_t uiPusher, target_t uiTarget, FXMVECTOR vecPosition)
{
	// Sync pusher
	engine::PushersInterpolate::Sync(rFrameInterpolate, uiPusher,
	{
		.vecPosition = vecPosition,
		.fRadius = kfSpaceshipPusherRadius,
		.fIntensity = kfSpaceshipPusherIntensity,
		.fPower = kfSpaceshipPusherPower,
		.flags = {engine::PusherFlags::kTypeDefault},
	});

	// Sync target
	if (uiTarget.IsValid())
	{
		TargetsInterpolate::Sync(rFrameInterpolate, uiTarget,
		{
			.vecPosition = vecPosition,
			.uiTypeIndex = gSpaceshipTargetTypeIndex,
		});
	}
}

static void RegisterSpaceshipTargetType()
{
	if (gSpaceshipTargetTypeIndex != 0xFF)
	{
		return;
	}

	TargetsPostRender::RegisterType(gSpaceshipTargetTypeIndex,
	{
		.crc = data::kTexturesBC4TargetpngCrc,
		.fSize = kfTargetSize,
		.fAlpha = kfTargetAlpha,
	});
}

#if defined(BT_CLIENT)
static void RegisterSpaceshipHitFlashEffect()
{
	if (suiSpaceshipHitFlashTypeIndex == 255)
	{
		engine::PointLightsInterpolate::RegisterType(suiSpaceshipHitFlashTypeIndex,
		{
			.crc = data::kTexturesBlasterBC74pngCrc,
			.uiColor = 0xFFFFFFFF,
		});

		engine::PointLightsInterpolate::RegisterControllerType(gSpaceshipHitFlashControllerTypeIndex,
		{
			.uiBaseTypeIndex = suiSpaceshipHitFlashTypeIndex,
			.uiKeyframeCount = 2,
			.bDestroysSelf = true,
			.pfTimes = {0.0f, kfHitFlashDuration, 0.0f, 0.0f},
			.keyframes =
			{
				{.fVisibleArea = kfHitFlashVisibleArea, .fVisibleIntensity = kfHitFlashVisibleIntensity, .fLightingArea = kfHitFlashLightingArea, .fLightingIntensity = kfHitFlashLightingIntensity, .fRotation = 0.0f},
				{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
				{},
				{},
			},
		});
	}
}
#endif // BT_CLIENT

void SpawnSpaceshipExplosion(Frame& __restrict rFrame, XMVECTOR vecPosition, XMVECTOR vecDirection, float fPercent)
{
	XMVECTOR vecJitteredPosition = common::RandomPositionJitter<kfSpaceshipExplosionPositionJitter>(vecPosition, rFrame.postRender.randomEngine);
	XMVECTOR vecJitteredDirection = common::RandomDirectionJitter<kfSpaceshipExplosionDirectionJitter>(vecDirection, rFrame.postRender.randomEngine);

	engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
	{
		.uiTypeIndex = gSpaceshipExplosionTypeIndex,
		.vecPosition = vecJitteredPosition,
		.vecDirection = vecJitteredDirection,
		.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kRed},
		.uiTrailCount = kuiSpaceshipExplosionTrailCount,
		.fTrailAngle = fPercent * XM_PI,
		.uiParticleCount = static_cast<uint32_t>(fPercent * kfSpaceshipExplosionParticleCount),
		.fParticleAngle = fPercent * XM_PIDIV2,
		.fLightPercent = fPercent * kfSpaceshipExplosionIntensity,
		.fSizePercent = fPercent * kfSpaceshipExplosionSizeStart + (1.0f - fPercent) * kfSpaceshipExplosionSizeEnd,
		.fSmokePercent = fPercent * kfSpaceshipExplosionSmoke,
		.fTimePercent = fPercent,
	});
}

void SpaceshipsInterpolate::AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateAllocateAndCopySpaceships);

	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiPushers, rPrevious.puiPushers, rCurrent.iCount * sizeof(rCurrent.puiPushers[0]));
		std::memcpy(rCurrent.puiTargets, rPrevious.puiTargets, rCurrent.iCount * sizeof(rCurrent.puiTargets[0]));
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiWindTrails, rPrevious.puiWindTrails, rCurrent.iCount * sizeof(rCurrent.puiWindTrails[0]));
#endif
	}
}

void SpaceshipsPostRender::AllocateAndCopy(SpaceshipsPostRender& rCurrent, const SpaceshipsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Static fields - memcpy (never modified in Update)
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecDamageDirections, rPrevious.pVecDamageDirections, rCurrent.iCount * sizeof(rCurrent.pVecDamageDirections[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void SpaceshipsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	const FrameBounds bounds = ComputeFrameBounds(rFrame.postRender.vecArea);

	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kTransfer)) [[likely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Build transfer request
		TransferRequest request
		{
			.eType = StatusChangeType::kTransferSpaceship,
			.data = {
				.vecPosition = vecPosition,
				.vecDirection = rCurrentInterpolate.pVecDirections[i],
				.vecVelocity = rCurrentPostRender.pVecVelocities[i],
				.alignment = rCurrentPostRender.pAlignments[i],
				.fHealth = rCurrentPostRender.pfHealths[i],
				.fNextBlasterSpawnTime = rCurrentPostRender.pfNextBlasterSpawnTimes[i],
			},
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);

		// Remove owned objects
		if (rCurrentInterpolate.puiTargets[i].IsValid())
		{
			TargetsPostRender::Remove(rFrame, rCurrentInterpolate.puiTargets[i], {TargetFlags::kDestination});
		}
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
#if defined(BT_CLIENT)
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}
#endif

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kExploding) || rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		// Cleanup owned objects
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
#if defined(BT_CLIENT)
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}
#endif

		FILE_LOG(0, "[SpaceshipDestroy] i={} count={} tick={} lastPos=({:.2f},{:.2f})",
			i, rCurrentInterpolate.iCount, rFrame.interpolate.iTick,
			rCurrentInterpolate.pVecPositions[i].m128_f32[0], rCurrentInterpolate.pVecPositions[i].m128_f32[1]);
		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	const PlayersInterpolate& rPlayers = *rFrame.interpolate.pPlayers;
	const PlayersPostRender& rPlayersPostRender = *rFrame.postRender.pPlayers;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Spawn staggered explosions during death animation
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentPostRender.pfDestroyedExplosionTimes[i] <= 0.0f)
		{
			rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfSpaceshipDestroyExplosionInterval;

			float fPercent = rCurrentInterpolate.pfDestroyedTimes[i] / kfSpaceshipDestroyTime;
			XMVECTOR vecDirection = XMVector3Normalize(rCurrentPostRender.pVecVelocities[i]);
			SpawnSpaceshipExplosion(rFrame, rCurrentInterpolate.pVecPositions[i], vecDirection, fPercent);
			continue;
		}

		// Find nearest alive player for blaster targeting
		XMVECTOR vecNearestPlayer = XMVectorZero();
		if (!NearestAlivePlayerPosition(rPlayers, rPlayersPostRender, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer))
		{
			continue;
		}

		// Skip blaster firing if spaceship not visible to nearest player
		if (!FrameInterpolate::IsVisible(vecNearestPlayer, rCurrentInterpolate.pVecPositions[i]))
		{
			continue;
		}

		// Fire blasters at player when facing them
		XMVECTOR vecToPlayer = XMVectorSubtract(vecNearestPlayer, rCurrentInterpolate.pVecPositions[i]);
		XMVECTOR vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
		float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrentInterpolate.pVecDirections[i], vecToPlayerNormal));

		bool bSpawnBlaster = fAngleToPlayer <= kfSpawnBlasterPlayerAngle;

		if (rCurrentPostRender.pfNextBlasterSpawnTimes[i] < 0.0f && bSpawnBlaster)
		{
			rCurrentPostRender.pfNextBlasterSpawnTimes[i] = kfBlastersSpawnCooldown;

			// Spawn blaster
			XMVECTOR vecDirection = rCurrentInterpolate.pVecDirections[i];
			XMVECTOR vecBlasterVelocity = XMVectorScale(vecDirection, kfBlastersSpeed);
			XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

			BlastersPostRender::Spawn(rFrame,
			{
				.vecPosition = vecPosition,
				.vecVelocity = vecBlasterVelocity,
				.uiTypeIndex = suiEnemyBlasterTypeIndex,
				.flags = {},
				.alignment = rCurrentPostRender.pAlignments[i],
				.fWindTrailIntensity = engine::gWindDepositSpaceshipsBlastersIntensity.Get(),
				.fWindTrailWidth = engine::gWindDepositSpaceshipsBlastersWidth.Get(),
				.fWindTrailLengthMultiplier = engine::gWindDepositSpaceshipsBlastersLengthMultiplier.Get(),
			});
		}
	}
}

#if defined(BT_CLIENT)
void SpaceshipsInterpolate::ClientInit(Frame& rFrame, int64_t iIndex)
{
	SpaceshipsInterpolate& rSpaceships = *rFrame.interpolate.pSpaceships;

	// Add client-only owned objects
	rSpaceships.puiWindTrails[iIndex] = {};
	engine::WindTrailsPostRender::Add(rFrame, rSpaceships.puiWindTrails[iIndex]);

	// Sync wind trail
	engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rSpaceships.puiWindTrails[iIndex],
	{
		.vecPosition = rSpaceships.pVecPositions[iIndex],
		.fIntensity = engine::gWindDepositSpaceshipsIntensity.Get(),
		.fWidth = engine::gWindDepositSpaceshipsWidth.Get(),
		.fLengthMultiplier = engine::gWindDepositSpaceshipsLengthMultiplier.Get(),
	});
}

void SpaceshipsInterpolate::ClientInitAll(Frame& rFrame)
{
	SpaceshipsInterpolate& rSpaceships = *rFrame.interpolate.pSpaceships;
	for (int64_t i = 0; i < rSpaceships.iCount; ++i)
	{
		if (rSpaceships.pfDestroyedTimes[i] >= 0.0f)
		{
			continue;
		}
		ClientInit(rFrame, i);
	}
}
#endif // BT_CLIENT

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = XMVectorSetW(rInfo.vecPosition, 1.0f);
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = -1.0f; // Sentinel: -1.0f = not exploding
	rCurrentInterpolate.pfDeltaRotations[iIndex] = 0.0f;
	rCurrentInterpolate.pfFreezeTimes[iIndex] = 0.0f;
#if defined(BT_CLIENT)
	rCurrentInterpolate.pfAnimationTimes[iIndex] = 0.0f;
#endif

	// Create owned pusher
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);

	// Create owned wind deposit
#if defined(BT_CLIENT)
	SpaceshipsInterpolate::ClientInit(rFrame, iIndex);
#endif

	// Create owned target for missile tracking (also creates its billboard)
	rCurrentInterpolate.puiTargets[iIndex] = {};
	TargetsPostRender::Add(rFrame, rCurrentInterpolate.puiTargets[iIndex], gSpaceshipTargetTypeIndex, rInfo.alignment);

	// Set target flags (PostRender field, not part of Sync)
	int64_t iTargetIndex = rFrame.interpolate.pTargets->IdToIndex(rCurrentInterpolate.puiTargets[iIndex]);
	rFrame.postRender.pTargets->pFlags[iTargetIndex] = {TargetFlags::kDestination};

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = {};
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecDamageDirections[iIndex] = XMVectorZero();
	rCurrentPostRender.pfHealths[iIndex] = rInfo.fHealth > 0.0f ? rInfo.fHealth : kfSpaceshipHealth;
	rCurrentPostRender.pfDestroyedExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfNextBlasterSpawnTimes[iIndex] = rInfo.fNextBlasterSpawnTime;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;

	// Sync owned objects after Add()
	SyncSpaceship(rFrame.interpolate, rCurrentInterpolate.puiPushers[iIndex], rCurrentInterpolate.puiTargets[iIndex], rInfo.vecPosition);
}

bool SpaceshipsInterpolate::operator==(const SpaceshipsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		bEqual &= common::BreakOnNotEqual(puiPushers[i], rOther.puiPushers[i]);
		bEqual &= common::BreakOnNotEqual(puiTargets[i], rOther.puiTargets[i]);
#if defined(BT_CLIENT)
		bEqual &= common::BreakOnNotEqual(puiWindTrails[i], rOther.puiWindTrails[i]);
#endif
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
#if defined(BT_CLIENT)
		bEqual &= common::BreakOnNotEqual(pfAnimationTimes[i], rOther.pfAnimationTimes[i]);
#endif
	}

	return bEqual;
}

bool SpaceshipsPostRender::operator==(const SpaceshipsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::BreakOnNotEqual(pVecDamageDirections[i], rOther.pVecDamageDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfHealths[i], rOther.pfHealths[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfNextBlasterSpawnTimes[i], rOther.pfNextBlasterSpawnTimes[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

bool SpaceshipsInterpolate::ServerCompare(const SpaceshipsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		bEqual &= common::BreakOnNotEqual(puiPushers[i], rOther.puiPushers[i]);
		bEqual &= common::BreakOnNotEqual(puiTargets[i], rOther.puiTargets[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
	}

	return bEqual;
}

bool SpaceshipsPostRender::ServerCompare(const SpaceshipsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::BreakOnNotEqual(pVecDamageDirections[i], rOther.pVecDamageDirections[i]);
		bEqual &= common::BreakOnNotEqual(pfHealths[i], rOther.pfHealths[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfNextBlasterSpawnTimes[i], rOther.pfNextBlasterSpawnTimes[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

} // namespace game
