// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Spaceships.h"

#include "Audio/AudioManager.h"
#include "File/FileManager.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Frame/Render.h"
#include "Graphics/AnimationData.h"
#include "Graphics/Camera.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Targets.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Multithreading.h"

#include "Data/Audio.h"
#include "Data/Scene.h"
#include "Data/Texture.h"

namespace game
{

#if 1
constexpr common::crc_t kModel = data::kModelsSpaceshipscenegltfCrc;
constexpr float kfSize = 0.0035f;
#endif
#if 0
constexpr common::crc_t kModel = data::kModelschernovan_nemesisscenegltfCrc;
constexpr float kfSize = 0.3f;
#endif

using enum SpaceshipFlags;

// Collision layer index (set each frame in PreCollision)
static inline size_t suiCollisionLayerIndex = 0;
static inline std::vector<engine::CollisionFlags_t> sCollisionFlags;
static inline std::vector<float> sCollisionRadii;
static inline std::vector<float> sCollisionDamages;

// Spaceship hit flash effect
static uint8_t suiSpaceshipHitFlashTypeIndex = 255;
static uint8_t suiSpaceshipHitFlashControllerTypeIndex = 255;

// Forward declarations for registration functions (called from Register())
static void RegisterEnemyBlasterType();
static void RegisterSpaceshipTargetType();
static void RegisterSpaceshipHitFlashEffect();

// Destroy timing
constexpr float kfDestroyTime = 0.25f;
constexpr float kfDestroyExplosionInterval = 0.024f;

// Spaceship explosion type
static uint8_t suiSpaceshipExplosionTypeIndex = 0xFF;

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

// Hit flash effect
constexpr float kfHitFlashDuration = 0.3f;
constexpr float kfHitFlashVisibleArea = 0.5f;
constexpr float kfHitFlashVisibleIntensity = 1.0f;
constexpr float kfHitFlashLightingArea = 1.0f;
constexpr float kfHitFlashLightingIntensity = 30.0f;

// Spaceship explosion spawn
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 8.0f;
constexpr float kfExplosionSizeStart = 1.25f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.5f;
constexpr float kfExplosionPositionJitter = 0.75f;
constexpr float kfExplosionDirectionJitter = 0.5f;
constexpr uint32_t kuiExplosionTrailCount = 5;

// Spaceship pusher
constexpr float kfSpaceshipPusherRadius = 3.0f;
constexpr float kfSpaceshipPusherIntensity = 150.0f;
constexpr float kfSpaceshipPusherPower = 1.0f;

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

// Spaceship audio
constexpr float kfDeathPitchMin = 0.75f;
constexpr float kfDeathPitchRandom = 0.5f;
constexpr float kfDeathExplosionVolume = 0.4f;
constexpr float kfHitSoundVolume = 0.2f;

// Terrain avoidance
constexpr int64_t kiFrontSamples = 4;
constexpr float kfFrontSamplesStep = 4.0f;
constexpr int64_t kiSideSamples = 2;
constexpr float kfSideSamplesStep = 2.0f;
constexpr float kfStepReduceWeight = 0.1f;
constexpr float kfAvoidTerrainMin = 0.5f;
constexpr float kfAvoidTerrainMax = 2.5f;
constexpr float kfAvoidTerrainDeltaAngleMin = 16.0f;
constexpr float kfAvoidTerrainDeltaAngleMax = 32.0f;
constexpr float kfDeltaAngleChangeAvoidTerrain = 0.995f;
constexpr float kfIgnoreAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfIgnoreAvoidTerrainPlayerDistance = 40.0f;

// Spaceship rendering
constexpr float kfRoll = 0.2f;
constexpr float kfFreezeTimeBlaster = 0.025f;

// Find the nearest alive (non-exploding) player position. Returns false if no alive players exist.
[[nodiscard]] static bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult)
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

void SpaceshipsInterpolate::AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateAllocateAndCopySpaceships);

	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiPushers, rPrevious.puiPushers, rCurrent.iCount * sizeof(rCurrent.puiPushers[0]));
		std::memcpy(rCurrent.puiTargets, rPrevious.puiTargets, rCurrent.iCount * sizeof(rCurrent.puiTargets[0]));
		std::memcpy(rCurrent.puiWindTrails, rPrevious.puiWindTrails, rCurrent.iCount * sizeof(rCurrent.puiWindTrails[0]));
	}
}

void SpaceshipsInterpolate::Register()
{
	// Spaceship explosion type
	engine::ExplosionsInterpolate::RegisterType(suiSpaceshipExplosionTypeIndex,
	{
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
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
	RegisterSpaceshipHitFlashEffect();
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

	// Register blaster type with area light
	BlastersInterpolate::RegisterType(suiEnemyBlasterTypeIndex,
	{
		.f2Size = {kfEnemyBlasterSize, kfEnemyBlasterSize},
		.uiAreaLightTypeIndex = suiEnemyBlasterAreaLightTypeIndex,
	});
}

// Target type registration for spaceship tracking
static uint8_t suiSpaceshipTargetTypeIndex = 0xFF;

// Helper to sync owned objects for a spaceship
static void XM_CALLCONV SyncSpaceship(FrameInterpolate& rFrameInterpolate, engine::pusher_t uiPusher, target_t uiTarget, FXMVECTOR vecPosition)
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
			.uiTypeIndex = suiSpaceshipTargetTypeIndex,
		});
	}
}

static void RegisterSpaceshipTargetType()
{
	if (suiSpaceshipTargetTypeIndex != 0xFF)
	{
		return;
	}

	TargetsPostRender::RegisterType(suiSpaceshipTargetTypeIndex,
	{
		.crc = data::kTexturesBC4TargetpngCrc,
		.fSize = kfTargetSize,
		.fAlpha = kfTargetAlpha,
	});
}

static void RegisterSpaceshipHitFlashEffect()
{
	if (suiSpaceshipHitFlashTypeIndex == 255)
	{
		engine::PointLightsInterpolate::RegisterType(suiSpaceshipHitFlashTypeIndex,
		{
			.crc = data::kTexturesBlasterBC74pngCrc,
			.uiColor = 0xFFFFFFFF,
		});

		engine::PointLightsInterpolate::RegisterControllerType(suiSpaceshipHitFlashControllerTypeIndex,
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

static void SpawnSpaceshipExplosion(Frame& __restrict rFrame, XMVECTOR vecPosition, XMVECTOR vecDirection, float fPercent)
{
	XMVECTOR vecJitteredPosition = common::RandomPositionJitter<kfExplosionPositionJitter>(vecPosition, rFrame.postRender.randomEngine);
	XMVECTOR vecJitteredDirection = common::RandomDirectionJitter<kfExplosionDirectionJitter>(vecDirection, rFrame.postRender.randomEngine);

	engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
	{
		.uiTypeIndex = suiSpaceshipExplosionTypeIndex,
		.vecPosition = vecJitteredPosition,
		.vecDirection = vecJitteredDirection,
		.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kRed},
		.uiTrailCount = kuiExplosionTrailCount,
		.fTrailAngle = fPercent * XM_PI,
		.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
		.fParticleAngle = fPercent * XM_PIDIV2,
		.fLightPercent = fPercent * kfExplosionIntensity,
		.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
		.fSmokePercent = fPercent * kfExplosionSmoke,
		.fTimePercent = fPercent,
	});
}

void SpaceshipsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerInterpolateUpdateSpaceships);

	SpaceshipsInterpolate& rCurrent = rCurrentFrameInterpolate.spaceships;
	const SpaceshipsInterpolate& rPrevious = rPreviousFrame.interpolate.spaceships;
	const SpaceshipsPostRender& rPreviousPostRender = rPreviousFrame.postRender.spaceships;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	// Hoist animation duration lookup outside the loop
	float fAnimationDuration = 0.0f;
	if (engine::gAnimationDataMap.contains(kModel))
	{
		fAnimationDuration = engine::gAnimationDataMap.at(kModel).mpAnimations[0].fDuration;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fFreezeTime = rPrevious.pfFreezeTimes[i];
		float fAnimationTime = rPrevious.pfAnimationTimes[i];

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
		if (fAnimationDuration > 0.0f)
		{
			fAnimationTime += fDeltaTime;
			if (fAnimationTime >= fAnimationDuration)
			{
				fAnimationTime = std::fmod(fAnimationTime, fAnimationDuration);
			}
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfFreezeTimes[i] = fFreezeTime;
		rCurrent.pfAnimationTimes[i] = fAnimationTime;

		// Sync owned objects (IDs copied in AllocateAndCopy)
		SyncSpaceship(rCurrentFrameInterpolate, rCurrent.puiPushers[i], rCurrent.puiTargets[i], vecPosition);

		// Sync wind deposit
		if (rCurrent.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsInterpolate::Sync(rCurrentFrameInterpolate, rCurrent.puiWindTrails[i],
			{
				.vecPosition = vecPosition,
				.fIntensity = engine::gWindDepositSpaceshipsIntensity.Get(),
				.fWidth = engine::gWindDepositSpaceshipsWidth.Get(),
				.fLengthMultiplier = engine::gWindDepositSpaceshipsLengthMultiplier.Get(),
			}, false);
		}
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

void SpaceshipsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateSpaceships);

	SpaceshipsPostRender& __restrict rCurrent = rFrame.postRender.spaceships;
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	const SpaceshipsPostRender& rPrevious = rPreviousFrame.postRender.spaceships;
	const SpaceshipsInterpolate& rPreviousInterpolate = rPreviousFrame.interpolate.spaceships;
	const PlayersInterpolate& rPlayers = rPreviousFrame.interpolate.players;
	const PlayersPostRender& rPlayersPostRender = rPreviousFrame.postRender.players;
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
		float fTerrainElevation = engine::gpIslands->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
		if (fTerrainElevation >= XMVectorGetZ(rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
		{
			XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslands->GlobalNormal(rCurrentInterpolate.pVecPositions[i]), 0.0f));

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

	SpaceshipsPostRender::AvoidTerrain(rFrame, rPreviousFrame, 0, rFrame.interpolate.spaceships.iCount);
}

void SpaceshipsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

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
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kExploding) || rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		// Cleanup owned objects
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	const PlayersInterpolate& rPlayers = rFrame.interpolate.players;
	const PlayersPostRender& rPlayersPostRender = rFrame.postRender.players;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Spawn staggered explosions during death animation
		if ((rCurrentPostRender.pFlags[i] & kExploding) && rCurrentPostRender.pfDestroyedExplosionTimes[i] <= 0.0f)
		{
			rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;

			float fPercent = rCurrentInterpolate.pfDestroyedTimes[i] / kfDestroyTime;
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

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = -1.0f; // Sentinel: -1.0f = not exploding
	rCurrentInterpolate.pfDeltaRotations[iIndex] = 0.0f;
	rCurrentInterpolate.pfFreezeTimes[iIndex] = 0.0f;
	rCurrentInterpolate.pfAnimationTimes[iIndex] = 0.0f;

	// Create owned pusher
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);

	// Create owned wind deposit
	rCurrentInterpolate.puiWindTrails[iIndex] = {};
	engine::WindTrailsPostRender::Add(rFrame, rCurrentInterpolate.puiWindTrails[iIndex]);
	engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.puiWindTrails[iIndex],
	{
		.vecPosition = rInfo.vecPosition,
		.fIntensity = engine::gWindDepositSpaceshipsIntensity.Get(),
		.fWidth = engine::gWindDepositSpaceshipsWidth.Get(),
		.fLengthMultiplier = engine::gWindDepositSpaceshipsLengthMultiplier.Get(),
	}, true);

	// Create owned target for missile tracking (also creates its billboard)
	rCurrentInterpolate.puiTargets[iIndex] = {};
	TargetsPostRender::Add(rFrame, rCurrentInterpolate.puiTargets[iIndex], suiSpaceshipTargetTypeIndex, rInfo.alignment);

	// Set target flags (PostRender field, not part of Sync)
	int64_t iTargetIndex = rFrame.interpolate.targets.IdToIndex(rCurrentInterpolate.puiTargets[iIndex]);
	rFrame.postRender.targets.pFlags[iTargetIndex] = {TargetFlags::kDestination};

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

static void XM_CALLCONV BeginExplosion(Frame& rFrame, int64_t i, FXMVECTOR vecDamageDirection)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	rCurrentPostRender.pFlags[i].Set(kExploding);
	rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;
	rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;

	// Store damage direction for knockback
	rCurrentPostRender.pVecDamageDirections[i] = vecDamageDirection;

	// Remove target so missiles stop tracking
	TargetsPostRender::Remove(rFrame, rCurrentInterpolate.puiTargets[i], {TargetFlags::kDestination});
	rCurrentInterpolate.puiTargets[i] = {};

	// Play explosion audio
	float fPitch = kfDeathPitchMin + common::Random<kfDeathPitchRandom>(rFrame.postRender.randomEngine);
	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], kfDeathExplosionVolume, fPitch);

	XMVECTOR vecDirection = XMVector3Normalize(rCurrentPostRender.pVecVelocities[i]);
	SpawnSpaceshipExplosion(rFrame, rCurrentInterpolate.pVecPositions[i], vecDirection, 1.0f);
}

void SpaceshipsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Heap: static vectors resized each frame, only allocates on first call or when count grows (capacity retained).
	// .data() pointers are passed to AddLayer and must survive until PostCollision, so workbuffer can't be used
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

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
		.uiCollidesWith = CollisidesWith::kSpaceship,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void SpaceshipsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

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
			const std::vector<engine::CollisionResult>* pCollisions = engine::Collision::GetCollisions(suiCollisionLayerIndex, i);
			for (const engine::CollisionResult& rResult : *pCollisions)
			{
				if (rResult.uiOtherCategory == CollisionCategory::kBlaster)
				{
					rCurrentPostRender.pfHealths[i] -= rResult.fDamageReceived;

					// Play hit sound
					engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster793907__cvltiv8r__snaresbycvltiv8r301wavCrc, rCurrentInterpolate.pVecPositions[i], kfHitSoundVolume);

					// Spawn hit flash effect at collision point
					engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiSpaceshipHitFlashControllerTypeIndex, rResult.vecContactPoint, 0.0f);

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
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		// Skip already exploding or transferring spaceships
		if ((rCurrentPostRender.pFlags[i] & kExploding) || (rCurrentPostRender.pFlags[i] & kTransfer))
		{
			continue;
		}

		// Query area damage from missiles (filter by kMissile category)
		XMVECTOR vecClosestSource {};
		float fDamage = engine::Collision::GetAreaDamage(rCurrentInterpolate.pVecPositions[i], CollisionCategory::kMissile, vecClosestSource);

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
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		// Skip terrain avoidance if close to nearest alive player and facing them
		XMVECTOR vecNearestPlayer = XMVectorZero();
		if (NearestAlivePlayerPosition(rFrame.interpolate.players, rFrame.postRender.players, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer))
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

		// Sample terrain elevation in front and to sides
		XMVECTOR vecLeftDirection = XMVector3Cross(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rCurrentInterpolate.pVecDirections[i]);
		float fLeftElevation = 0.0f;
		float fRightElevation = 0.0f;
		float fTotalWeight = 0.0f;

		for (int64_t j = 0; j < kiFrontSamples; ++j)
		{
			float fWeightFront = 1.0f - static_cast<float>(j) * kfStepReduceWeight;
			XMVECTOR vecSamplePosition = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(j + 1) * kfFrontSamplesStep), rCurrentInterpolate.pVecDirections[i], rCurrentInterpolate.pVecPositions[i]);

			for (int64_t k = 0; k < kiSideSamples; ++k)
			{
				float fWeight = fWeightFront - static_cast<float>(k) * kfStepReduceWeight;
				fTotalWeight += fWeight;

				XMVECTOR vecSamplePositionLeft = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
				fLeftElevation += fWeight * engine::gpIslands->GlobalElevation(vecSamplePositionLeft);

				XMVECTOR vecSamplePositionRight = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * -kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
				fRightElevation += fWeight * engine::gpIslands->GlobalElevation(vecSamplePositionRight);
			}
		}

		float fTotalWeightInverse = 1.0f / fTotalWeight;
		fLeftElevation *= fTotalWeightInverse;
		fRightElevation *= fTotalWeightInverse;

		// Adjust rotation to avoid terrain
		if (fLeftElevation > kfAvoidTerrainMin || fRightElevation > kfAvoidTerrainMin)
		{
			float fPercent = fLeftElevation > fRightElevation ? (fLeftElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax : (fRightElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax;

			float fAvoidDeltaAngle = (1.0f - fPercent) * kfAvoidTerrainDeltaAngleMin + fPercent * kfAvoidTerrainDeltaAngleMax;
			float fWantedDeltaRotation = fLeftElevation > fRightElevation ? -fAvoidDeltaAngle : fAvoidDeltaAngle;
			rCurrentInterpolate.pfDeltaRotations[i] = kfDeltaAngleChangeAvoidTerrain * rCurrentInterpolate.pfDeltaRotations[i] + (1.0f - kfDeltaAngleChangeAvoidTerrain) * fWantedDeltaRotation;
		}

		// Clamp delta rotation
		rCurrentInterpolate.pfDeltaRotations[i] = common::MinAbs(rCurrentInterpolate.pfDeltaRotations[i], kfDeltaAngleMax);
	}
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
		bEqual &= common::BreakOnNotEqual(puiWindTrails[i], rOther.puiWindTrails[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfAnimationTimes[i], rOther.pfAnimationTimes[i]);
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

void SpaceshipsInterpolate::GraphicsResources()
{
	engine::Buffer* pStorageBuffers = engine::gpBufferManager->CreateDynamicBuffer(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout));
	engine::gpPipelineManager->CreateDynamicModelPipeline(kCrc, kName, kModel, pStorageBuffers);
	engine::gpPipelineManager->CreateDynamicModelPipelineShadow(kCrc, kName, kModel, pStorageBuffers);
}

void SpaceshipsInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerRenderSpaceships);

	const SpaceshipsInterpolate& rCurrent = rFrameInterpolate.spaceships;
	gpProfileManager->SetCount(game::kCpuCounterSpaceships, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	int64_t iFramebuffer = iCommandBuffer;
	if (engine::Buffer* pBuffer = engine::gpBufferManager->ResizeDynamicBufferIfNeeded(kCrc, engine::kBufferMain, kName, sizeof(shaders::ModelLayout), rCurrent.iCapacity, iCommandBuffer))
	{
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, pBuffer);
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->UpdateStorageBufferDescriptors(iFramebuffer, 2, pBuffer);
	}

	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationY(0.0f) * XMMatrixRotationZ(XM_PIDIV2);

	auto [pLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	// Look up animation data and chunk info (hoisted outside loop)
	const engine::AnimationData* pAnimationData = nullptr;
	uint32_t uiMaterialCount = 0;
	int64_t iSkinnedMaterialCount = 0;
	if (engine::gAnimationDataMap.contains(kModel))
	{
		pAnimationData = &engine::gAnimationDataMap.at(kModel);
		uiMaterialCount = engine::gpFileManager->GetEagerChunkMap().at(kModel).pHeader->sceneHeader.uiMaterialCount;

		iSkinnedMaterialCount = pAnimationData->SkinnedMaterialCount(uiMaterialCount);
	}

	// Pass 1: Visibility cull (main thread) — build compacted visible index list
	int64_t* pVisibleIndices = common::gpThreadLocal->mWorkbuffer.PushBuffer<int64_t*>(rCurrent.iCount * static_cast<int64_t>(sizeof(int64_t)));
	int64_t iVisibleCount = 0;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pfDestroyedTimes[i] == 0.0f)
		{
			continue;
		}

		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!gpCamera->InVisibleArea(gpCamera->f4RenderVisibleArea, f4Position))
		{
			continue;
		}

		pVisibleIndices[iVisibleCount++] = i;
	}

	// Bulk skinning pre-allocation (main thread) — one call instead of N per-spaceship calls
	int64_t iMeshDataBase = 0;
	int64_t iJointBase = 0;
	common::MeshData* pMeshDataBuffer = nullptr;
	common::JointMatrix* pJointMatricesBuffer = nullptr;
	int64_t iJointsPerShip = 0;

	if (pAnimationData != nullptr && iVisibleCount > 0)
	{
		iMeshDataBase = engine::gpBufferManager->AllocateMeshData(iCommandBuffer, iVisibleCount * uiMaterialCount);
		pMeshDataBuffer = reinterpret_cast<common::MeshData*>(engine::gpBufferManager->mMeshDataStorageBuffers.at(iCommandBuffer).mpMappedMemory);

		iJointsPerShip = iSkinnedMaterialCount * pAnimationData->mHeader.skeleton.uiSkinJointCount;
		int64_t iTotalJoints = iVisibleCount * iJointsPerShip;
		if (iTotalJoints > 0)
		{
			iJointBase = engine::gpBufferManager->AllocateJointMatrices(iCommandBuffer, iTotalJoints);
		}
		pJointMatricesBuffer = reinterpret_cast<common::JointMatrix*>(engine::gpBufferManager->mJointMatrixStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	}

	// Per-range processing lambda — each visible index j writes to deterministic non-overlapping output slots
	auto processRange = [&](int64_t iStart, int64_t iEnd)
	{
		for (int64_t j = iStart; j < iEnd; ++j)
		{
			int64_t i = pVisibleIndices[j];

			float fSize = kfSize;
			if (rCurrent.pfDestroyedTimes[i] > 0.0f)
			{
				fSize *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.75f);
			}

			XMFLOAT4A f4Position {};
			XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);

			XMMATRIX matScaling = XMMatrixScaling(fSize, fSize, fSize);
			XMMATRIX matRoll = XMMatrixRotationX(-kfRoll * rCurrent.pfDeltaRotations[i]);
			XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
			XMMATRIX matTransform = matScaling * sMatPreRotate * matRoll * matYaw * matTranslation;

			shaders::ModelLayout& rModelLayout = pLayouts[j];
			rModelLayout.f4Position = f4Position;
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4Transform[0]), matTransform);
			XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

			float fFreezeColor = std::clamp(rCurrent.pfFreezeTimes[i] / kfFreezeTimeBlaster, 0.0f, 1.0f);
			rModelLayout.f4ColorAdd = {0.5f * fFreezeColor, 0.25f * fFreezeColor, 0.25f * fFreezeColor, 0.0f};
			rModelLayout.uiMeshDataBase = 0;

			if (pAnimationData != nullptr)
			{
				int64_t iShipMeshDataBase = iMeshDataBase + j * uiMaterialCount;
				rModelLayout.uiMeshDataBase = static_cast<uint32_t>(iShipMeshDataBase);

				common::MeshData* pMeshData = pMeshDataBuffer + iShipMeshDataBase;

				int64_t iJointMatrixOffset = iJointBase + j * iJointsPerShip;

				pAnimationData->EvaluateAnimation(0, rCurrent.pfAnimationTimes[i], uiMaterialCount, pMeshData, pJointMatricesBuffer, iJointMatrixOffset);
			}
		}
	};

	gpProfileManager->GetCpuTimer(game::kCpuTimerRenderSpaceships).iThreads = common::gpMultithreading->WorkerCount() + 1;
	common::gpMultithreading->Dispatch(iVisibleCount, processRange);

	gpProfileManager->SetCount(game::kCpuCounterSpaceshipsRendered, iVisibleCount);

	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iVisibleCount);
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iVisibleCount);

	common::gpThreadLocal->mWorkbuffer.Pop();
}

} // namespace game
