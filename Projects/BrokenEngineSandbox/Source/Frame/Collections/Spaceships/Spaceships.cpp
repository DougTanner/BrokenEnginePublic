#include "Spaceships.h"

#include "Data/Audio.h"
#include "Data/Texture.h"
#include "Frame/FrameStaticData.h"
#include "Frame/HealthDamage.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Pushers/Pushers.h"
#include "Frame/Collections/Targets/Targets.h"

#if defined(BT_CLIENT)
#include "Data/Scene.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Ui/LightingWrappers.h"
#endif

namespace engine
{
template struct Collection<game::SpaceshipsInterpolate>;
template struct Collection<game::SpaceshipsPostRender>;
}

namespace game
{

using enum SpaceshipFlags;

// Explosion
constexpr float kfSpaceshipExplosionIntensity = 1.5f;
constexpr float kfSpaceshipExplosionParticleCount = 8.0f;
constexpr float kfSpaceshipExplosionSizeStart = kfSpaceshipRadius * 0.625f;
constexpr float kfSpaceshipExplosionSizeEnd = kfSpaceshipRadius * 0.25f;
constexpr float kfSpaceshipExplosionSmoke = 0.5f;
constexpr float kfSpaceshipExplosionPositionJitter = kfSpaceshipRadius * 0.375f;
constexpr float kfSpaceshipExplosionDirectionJitter = 0.5f;
constexpr uint32_t kuiSpaceshipExplosionTrailCount = 5;

// Forward declarations for registration functions (called from Register())
static void RegisterEnemyBlasterType();
static void RegisterSpaceshipTargetType();

// Shared type indices (accessible from SpaceshipsCombat.cpp via extern)
uint8_t gSpaceshipExplosionTypeIndex = 0xFF;
uint8_t gSpaceshipTargetTypeIndex = 0xFF;
#if defined(BT_CLIENT)
static uint8_t suiSpaceshipHitFlashTypeIndex = 0xFF;
uint8_t gSpaceshipHitFlashControllerTypeIndex = 0xFF;

static void RegisterSpaceshipHitFlashEffect();
#endif

constexpr uint32_t kuiSpaceshipExplosionBaseParticleCount = 16;
constexpr uint32_t kuiSpaceshipExplosionParticleColor = 0xFF0000FF;
constexpr float kfSpaceshipExplosionParticleVelocityMin = 1.0f;
constexpr float kfSpaceshipExplosionParticleVelocityRandom = 9.0f;
constexpr float kfSpaceshipExplosionParticleVerticalVelocityMin = -5.0f;
constexpr float kfSpaceshipExplosionParticleVerticalVelocityRandom = 10.0f;
constexpr float kfSpaceshipExplosionParticleIntensityDecay = 2.4f;
// Particle lighting now in LightingWrappers (gSpaceshipExplosionParticle*)
constexpr float kfSpaceshipExplosionTrailLengthRandom = kfSpaceshipRadius * 1.25f;
constexpr uint32_t kuiSpaceshipExplosionSecondaryCount = 1;

// Enemy blaster
constexpr float kfSpawnBlasterPlayerAngle = 0.1f;
constexpr float kfBlastersSpeed = 50.0f;
constexpr float kfBlastersSpawnCooldown = 1.0f;

constexpr float kfEnemyBlasterSize = kfSpaceshipRadius * 0.15f;
// Enemy blaster lighting now in LightingWrappers (gEnemyBlaster*)

// Spaceship target
constexpr float kfTargetSize = kfSpaceshipRadius * 0.03f;
constexpr float kfTargetAlpha = 1.5f;

#if defined(BT_CLIENT)
// Hit flash effect timing
constexpr float kfHitFlashDuration = 0.3f;
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

		if (rPlayersPostRender.pfArrivalGracePeriods[i] > 0.0f)
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
#if defined(BT_CLIENT)
		.fParticleLightingSize = gSpaceshipExplosionParticleLightingArea.Get(),
		.fParticleLightingIntensity = gSpaceshipExplosionParticleLightingIntensity.Get(),
#endif
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
static uint8_t suiEnemyBlasterPointLightTypeIndex = 0xFF;
static uint8_t suiEnemyBlasterTypeIndex = 0xFF;

static void RegisterEnemyBlasterType()
{
	if (suiEnemyBlasterTypeIndex != 0xFF)
	{
		return;
	}

#if defined(BT_CLIENT)
	// Register camera-aligned point light type for enemy blasters
	engine::PointLightsInterpolate::RegisterType(suiEnemyBlasterPointLightTypeIndex,
	{
		.crc = data::kTexturesBlasterBC72pngCrc,
		.uiColor = 0xFFFFFFFF,
		.fVisibleArea = kfEnemyBlasterSize,
		.bCameraAligned = true,
		.pVisibleIntensityWrapper = &gEnemyBlasterVisibleIntensity,
		.pLightingAreaWrapper = &gEnemyBlasterLightingArea,
		.pLightingIntensityWrapper = &gEnemyBlasterLightingIntensity,
	});
#endif // BT_CLIENT

	// Register blaster type with point light
	BlastersInterpolate::RegisterType(suiEnemyBlasterTypeIndex,
	{
		.f2Size = {kfEnemyBlasterSize, kfEnemyBlasterSize},
		.uiPointLightTypeIndex = suiEnemyBlasterPointLightTypeIndex,
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
	if (suiSpaceshipHitFlashTypeIndex == 0xFF)
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
				{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
				{.fVisibleArea = 1.0f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 1.0f, .fRotation = 0.0f},
				{},
				{},
			},
			.ppVisibleAreaScales = {&gHitFlashVisibleAreaOne, &gHitFlashVisibleAreaTwo, nullptr, nullptr},
			.ppVisibleIntensityScales = {&gHitFlashVisibleIntensityOne, &gHitFlashVisibleIntensityTwo, nullptr, nullptr},
			.ppLightingAreaScales = {&gHitFlashLightingAreaOne, &gHitFlashLightingAreaTwo, nullptr, nullptr},
			.ppLightingIntensityScales = {&gHitFlashLightingIntensityOne, &gHitFlashLightingIntensityTwo, nullptr, nullptr},
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
		#if defined(BT_CLIENT)
			.fLightPercent = fPercent * gSpaceshipExplosionLightingIntensity.Get(),
#else
			.fLightPercent = fPercent * kfSpaceshipExplosionIntensity,
#endif
		.fSizePercent = fPercent * kfSpaceshipExplosionSizeStart + (1.0f - fPercent) * kfSpaceshipExplosionSizeEnd,
		.fSmokePercent = fPercent * kfSpaceshipExplosionSmoke,
		.fTimePercent = fPercent,
	});
}

#if defined(BT_CLIENT)
// Spaceship model (defined in SpaceshipsRender.cpp)
extern const common::crc_t kSpaceshipModel;
#endif

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

static void RemoveOwnedObjects(Frame& rFrame, SpaceshipsInterpolate& rCurrentInterpolate, int64_t i, bool bRemoveTarget)
{
	if (bRemoveTarget && rCurrentInterpolate.puiTargets[i].IsValid())
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
}

void SpaceshipsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

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
				.fArrivalGracePeriod = rCurrentPostRender.pfArrivalGracePeriods[i],
			},
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		// Heap realloc warning: capacity exceeded during burst transfers (e.g. network stall recovery). Increase kuiInitialTransferCapacity in Frame.h
		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);

		RemoveOwnedObjects(rFrame, rCurrentInterpolate, i, true);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kExploding) || rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		RemoveOwnedObjects(rFrame, rCurrentInterpolate, i, false);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
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
				.fWindTrailIntensity = game::gWindDepositSpaceshipsBlastersIntensity.Get(),
				.fWindTrailWidth = game::gWindDepositSpaceshipsBlastersWidth.Get(),
				.fWindTrailLengthMultiplier = game::gWindDepositSpaceshipsBlastersLengthMultiplier.Get(),
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
		.fIntensity = game::gWindDepositSpaceshipsIntensity.Get(),
		.fWidth = game::gWindDepositSpaceshipsWidth.Get(),
		.fLengthMultiplier = game::gWindDepositSpaceshipsLengthMultiplier.Get(),
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

	// Create owned target for missile tracking
	rCurrentInterpolate.puiTargets[iIndex] = {};
	TargetsPostRender::Add(rFrame, rCurrentInterpolate.puiTargets[iIndex], gSpaceshipTargetTypeIndex, rInfo.alignment);

	// Set target flags (PostRender field, not part of Sync)
	// Defer kDestination during arrival grace period so missiles don't target this spaceship
	int64_t iTargetIndex = rFrame.interpolate.pTargets->IdToIndex(rCurrentInterpolate.puiTargets[iIndex]);
	if (rInfo.fArrivalGracePeriod <= 0.0f)
	{
		rFrame.postRender.pTargets->pFlags[iTargetIndex] = {TargetFlags::kDestination};
	}

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = {};
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecDamageDirections[iIndex] = XMVectorZero();
	rCurrentPostRender.pfHealths[iIndex] = rInfo.fHealth > 0.0f ? rInfo.fHealth : kfSpaceshipHealth;
	rCurrentPostRender.pfDestroyedExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfNextBlasterSpawnTimes[iIndex] = rInfo.fNextBlasterSpawnTime;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;
	rCurrentPostRender.pfArrivalGracePeriods[iIndex] = rInfo.fArrivalGracePeriod;

	// Sync owned objects after Add()
	SyncSpaceship(rFrame.interpolate, rCurrentInterpolate.puiPushers[iIndex], rCurrentInterpolate.puiTargets[iIndex], rInfo.vecPosition);
}

void SpaceshipsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
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
		float fArrivalGracePeriod = std::max(0.0f, rPrevious.pfArrivalGracePeriods[i] - fDeltaTime);

		// Set kDestination on target when grace period expires
		if (rPrevious.pfArrivalGracePeriods[i] > 0.0f && fArrivalGracePeriod <= 0.0f && rCurrentInterpolate.puiTargets[i].IsValid())
		{
			int64_t iTargetIndex = rFrame.interpolate.pTargets->IdToIndex(rCurrentInterpolate.puiTargets[i]);
			rFrame.postRender.pTargets->pFlags[iTargetIndex].Set(TargetFlags::kDestination);
		}

		// Load from Interpolate (these are now in Interpolate)
		float fDeltaRotation = rPreviousInterpolate.pfDeltaRotations[i];
		float fFreezeTime = rPreviousInterpolate.pfFreezeTimes[i] - fDeltaTime;

		// Find nearest alive player (shared input for RegenerateHealth + ComputeSteering)
		XMVECTOR vecNearestPlayer = XMVectorZero();
		bool bPlayerAlive = NearestAlivePlayerPosition(rPlayers, rPlayersPostRender, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer);

		RegenerateHealth(rCurrentInterpolate.pVecPositions[i], bPlayerAlive, vecNearestPlayer, flags, fDeltaTime, fHealth);

		if (!(flags & kExploding)) [[likely]]
		{
			ComputeSteering(rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.pVecDirections[i], bPlayerAlive, vecNearestPlayer, fDeltaTime, flags, fDeltaRotation);
			ApplyMovement(rFrame, rCurrentInterpolate, i, flags, fDeltaTime, vecVelocity);
		}
		else
		{
			ApplyDeathKnockback(rPrevious.pVecDamageDirections[i], vecVelocity);
		}

		ApplyTerrainBounce(rCurrentInterpolate, i, fDeltaTime, fDeltaRotation, vecVelocity);

		// Clamp delta rotation
		fDeltaRotation = common::MinAbs(fDeltaRotation, kfSpaceshipMaxTurnRate);

		// Save to PostRender (static fields copied via memcpy in AllocateAndCopy)
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pfHealths[i] = fHealth;
		rCurrent.pfDestroyedExplosionTimes[i] = fDestroyedExplosionTime;
		rCurrent.pfNextBlasterSpawnTimes[i] = fNextBlasterSpawnTime;
		rCurrent.pfArrivalGracePeriods[i] = fArrivalGracePeriod;

		// Save to Interpolate (these are now in Interpolate)
		rCurrentInterpolate.pfDeltaRotations[i] = fDeltaRotation;
		rCurrentInterpolate.pfFreezeTimes[i] = fFreezeTime;
	}

	SpaceshipsPostRender::AvoidTerrain(rFrame, rPreviousFrame, 0, rFrame.interpolate.pSpaceships->iCount);
}

bool SpaceshipsInterpolate::LogDifferences(const SpaceshipsInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("SpaceshipsInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::LogDifference<"pfDestroyedTimes">(i, pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
		bEqual &= common::LogDifference<"puiPushers">(i, puiPushers[i], rOther.puiPushers[i]);
		bEqual &= common::LogDifference<"puiTargets">(i, puiTargets[i], rOther.puiTargets[i]);
		bEqual &= common::LogDifference<"pfDeltaRotations">(i, pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::LogDifference<"pfFreezeTimes">(i, pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
	}

	return bEqual;
}

bool SpaceshipsPostRender::LogDifferences(const SpaceshipsPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("SpaceshipsPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference_Vec("pVecVelocities", i, pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::LogDifference_Vec("pVecDamageDirections", i, pVecDamageDirections[i], rOther.pVecDamageDirections[i]);
		bEqual &= common::LogDifference<"pfHealths">(i, pfHealths[i], rOther.pfHealths[i]);
		bEqual &= common::LogDifference<"pfDestroyedExplosionTimes">(i, pfDestroyedExplosionTimes[i], rOther.pfDestroyedExplosionTimes[i]);
		bEqual &= common::LogDifference<"pfNextBlasterSpawnTimes">(i, pfNextBlasterSpawnTimes[i], rOther.pfNextBlasterSpawnTimes[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
		bEqual &= common::LogDifference<"pfArrivalGracePeriods">(i, pfArrivalGracePeriods[i], rOther.pfArrivalGracePeriods[i]);
	}

	return bEqual;
}

} // namespace game
