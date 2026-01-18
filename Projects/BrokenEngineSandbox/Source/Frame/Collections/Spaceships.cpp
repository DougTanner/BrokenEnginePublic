// Note: Not using precompiled header so that this file can be optimized in Debug builds
// #pragma optimize( "", off )
#include "Pch.h"

#include "Spaceships.h"

#include "Audio/AudioManager.h"
#include "Frame/Collections/Blasters.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Targets.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum SpaceshipFlags;

// Collision layer (set each frame in PreCollision)
static inline int64_t siCollisionLayerIndex = 0;
static inline std::vector<engine::CollisionFlags_t> sCollisionFlags;

// Spaceship hit flash effect
static uint8_t suiSpaceshipHitFlashTypeIndex = 255;
static uint8_t suiSpaceshipHitFlashControllerTypeIndex = 255;

// Explosion type registration
static uint8_t suiSpaceshipExplosionTypeIndex = 0xFF;

// Forward declarations for registration functions (called from Register())
static void RegisterEnemyBlasterType();
static void RegisterSpaceshipTargetType();
static void RegisterSpaceshipHitFlashEffect();

void SpaceshipsInterpolate::AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiPushers, rPrevious.puiPushers, static_cast<size_t>(rCurrent.iCount) * sizeof(engine::pusher_t));
		std::memcpy(rCurrent.puiTargets, rPrevious.puiTargets, static_cast<size_t>(rCurrent.iCount) * sizeof(target_t));
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
		.uiBaseParticleCount = 16,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = 5.0f,
		.fParticleVelocityRandom = 15.0f,
		.fPusherRadius = 4.0f,
		.fPusherIntensity = 15000.0f,
		.fTrailLengthRandom = 2.5f,
		.uiSecondaryExplosionCount = 1,
	});

	RegisterSpaceshipTargetType();
	RegisterEnemyBlasterType();
	RegisterSpaceshipHitFlashEffect();
}

void SpaceshipsInterpolate::GraphicsResources()
{
	AllocatePipelines();
}

// DT: TODO Move these into functions if possible
constexpr float kfDestroyTime = 0.25f;

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

// Blaster firing constants
constexpr float kfSpawnBlasterPlayerAngle = 0.1f;
constexpr float kfBlastersSpeed = 70.0f;
constexpr float kfBlastersSpawnInterval = 0.085f;
constexpr float kfBlastersSpawnCooldown = 1.0f;

// Explosion constants
constexpr float kfDestroyExplosionInterval = 0.024f;
constexpr float kfDeathKnockbackSpeed = 20.0f;
constexpr float kfExplosionIntensity = 1.5f;
constexpr float kfExplosionParticleCount = 8.0f;
constexpr float kfExplosionSizeStart = 1.25f;
constexpr float kfExplosionSizeEnd = 0.5f;
constexpr float kfExplosionSmoke = 0.5f;

// Visual polish constants
constexpr float kfFreezeTimeBlaster = 0.025f;
constexpr float kfRoll = 0.2f;
constexpr float kfDeltaAngleMax = 4.0f;
constexpr float kfVelocityToDirection = 4.0f;

// Terrain avoidance constants
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

// Terrain collision constants
constexpr float kfTerrainCollisionRotation = 8.0f;
constexpr float kfTerrainCollisionMovePosition = 4.0f;
constexpr float kfTerrainCollisionAddVelocity = 4.0f;

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
		.crc = data::kTexturesBlasterBC74pngCrc,
		.puiColors = {0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF},
		.pf2Texcoords = {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
		.fVisibleIntensity = 1.25f,
		.fLightingSize = 2.0f,
		.fLightingIntensity = 200.0f,
	});

	// Register blaster type with area light
	BlastersInterpolate::RegisterType(suiEnemyBlasterTypeIndex,
	{
		.f2Size = {0.25f, 0.55f},
		.uiAreaLightTypeIndex = suiEnemyBlasterAreaLightTypeIndex,
	});
}

// Target type registration for spaceship tracking
static uint8_t suiSpaceshipTargetTypeIndex = 0xFF;

// Pusher constants for spaceships
constexpr float kfSpaceshipPusherRadius = 3.0f;
constexpr float kfSpaceshipPusherIntensity = 150.0f;
constexpr float kfSpaceshipPusherPower = 1.0f;

// Helper to sync owned objects for a spaceship
static void XM_CALLCONV SyncSpaceship(
	FrameInterpolate& rFrameInterpolate,
	engine::pusher_t uiPusher,
	target_t uiTarget,
	FXMVECTOR vecPosition)
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
		.fSize = 0.06f,
		.fAlpha = 1.5f,
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
			.pfTimes = {0.0f, 0.3f, 0.0f, 0.0f},
			.keyframes =
			{
				{.fVisibleArea = 0.5f, .fVisibleIntensity = 1.0f, .fLightingArea = 1.0f, .fLightingIntensity = 30.0f, .fRotation = 0.0f},
				{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
				{},
				{},
			},
		});
	}
}

static void SpawnSpaceshipExplosion(Frame& __restrict rFrame, XMVECTOR vecPosition, XMVECTOR vecDirection, float fPercent)
{
	static constexpr float kfPositionJitter = 0.75f;
	XMVECTOR vecJitteredPosition = XMVectorAdd(
		XMVectorSet(
			-kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.postRender.randomEngine),
			-kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.postRender.randomEngine),
			0.0f, 0.0f),
		vecPosition);

	static constexpr float kfDirectionJitter = 0.5f;
	XMVECTOR vecJitteredDirection = XMVector3Normalize(XMVectorAdd(
		XMVectorSet(
			-kfDirectionJitter + common::Random<2.0f * kfDirectionJitter>(rFrame.postRender.randomEngine),
			-kfDirectionJitter + common::Random<2.0f * kfDirectionJitter>(rFrame.postRender.randomEngine),
			0.0f, 0.0f),
		vecDirection));

	engine::ExplosionsPostRender::Spawn(
		rFrame,
		rFrame.interpolate.fCurrentTime,
		{
			.uiTypeIndex = suiSpaceshipExplosionTypeIndex,
			.vecPosition = vecJitteredPosition,
			.vecDirection = vecJitteredDirection,
			.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kRed},
			.uiTrailCount = 5,
			.fTrailAngle = fPercent * XM_PI,
			.uiParticleCount = static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
			.fParticleAngle = fPercent * XM_PIDIV2,
			.fLightPercent = fPercent * kfExplosionIntensity,
			.fPusherPercent = 0.0f,
			.fSizePercent = fPercent * kfExplosionSizeStart + (1.0f - fPercent) * kfExplosionSizeEnd,
			.fSmokePercent = fPercent * kfExplosionSmoke,
			.fTimePercent = fPercent,
		});
}

void SpaceshipsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrent = rCurrentFrameInterpolate.spaceships;
	const SpaceshipsInterpolate& rPrevious = rPreviousFrame.interpolate.spaceships;
	const SpaceshipsPostRender& rPreviousPostRender = rPreviousFrame.postRender.spaceships;
	float fDeltaTime = rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fFreezeTime = rPrevious.pfFreezeTimes[i];

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

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfFreezeTimes[i] = fFreezeTime;

		// Sync owned objects (IDs copied in AllocateAndCopy)
		SyncSpaceship(
			rCurrentFrameInterpolate,
			rCurrent.puiPushers[i],
			rCurrent.puiTargets[i],
			vecPosition);
	}
}

void SpaceshipsPostRender::AllocateAndCopy(SpaceshipsPostRender& rCurrent, const SpaceshipsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void SpaceshipsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsPostRender& __restrict rCurrent = rFrame.postRender.spaceships;
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	const SpaceshipsPostRender& rPrevious = rPreviousFrame.postRender.spaceships;
	const SpaceshipsInterpolate& rPreviousInterpolate = rPreviousFrame.interpolate.spaceships;
	const PlayerInterpolate& rPlayer = rPreviousFrame.interpolate.player;
	float fDeltaTime = rFrame.interpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load from PostRender
		SpaceshipFlags_t flags = rPrevious.pFlags[i];
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
		float fHealth = rPrevious.pfHealths[i];
		float fDestroyedExplosionTime = rPrevious.pfDestroyedExplosionTimes[i] - fDeltaTime;
		float fNextBlasterSpawnTime = rPrevious.pfNextBlasterSpawnTimes[i] - fDeltaTime;
		int32_t iBlasterSpawn = rPrevious.piBlasterSpawns[i];

		// Load from Interpolate (these are now in Interpolate)
		float fDeltaRotation = rPreviousInterpolate.pfDeltaRotations[i];
		float fFreezeTime = rPreviousInterpolate.pfFreezeTimes[i] - fDeltaTime;

		if (!(flags & kExploding) && common::Distance(rCurrentInterpolate.pVecPositions[i], rPlayer.vecPosition) > 60.0f) [[unlikely]]
		{
			fHealth = std::min(fHealth + fDeltaTime * kfHealthRegen, kfSpaceshipHealth);
		}

		XMVECTOR vecToPlayer = XMVectorSubtract(rPlayer.vecPosition, rCurrentInterpolate.pVecPositions[i]);
		float fPlayerDistance = XMVectorGetX(XMVector3Length(vecToPlayer));
		if (fPlayerDistance < kfFleePlayerStart)
		{
			flags |= kFleePlayer;
		}
		else if (fPlayerDistance > kfFleePlayerEnd)
		{
			flags.Clear(kFleePlayer);
		}

		XMVECTOR vecIslandCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
		float fDistanceFromIslandCenter = common::Distance(rCurrentInterpolate.pVecPositions[i], vecIslandCenter);
		if (fDistanceFromIslandCenter > kfReturnDistance)
		{
			flags |= kReturnToIslandCenter;
		}
		else if (fDistanceFromIslandCenter < kfReturnedDistance)
		{
			flags.Clear(kReturnToIslandCenter);
		}

		XMVECTOR vecDestination = rPlayer.vecPosition;
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

		// Save to PostRender
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pVecDamageDirections[i] = rPrevious.pVecDamageDirections[i];
		rCurrent.pfHealths[i] = fHealth;
		rCurrent.pfDestroyedExplosionTimes[i] = fDestroyedExplosionTime;
		rCurrent.pfNextBlasterSpawnTimes[i] = fNextBlasterSpawnTime;
		rCurrent.piBlasterSpawns[i] = iBlasterSpawn;

		// Save to Interpolate (these are now in Interpolate)
		rCurrentInterpolate.pfDeltaRotations[i] = fDeltaRotation;
		rCurrentInterpolate.pfFreezeTimes[i] = fFreezeTime;
	}

	SpaceshipsPostRender::AvoidTerrain(rFrame, rPreviousFrame, 0, rFrame.interpolate.spaceships.iCount);
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

		// Cleanup owned pusher
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void SpaceshipsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	const PlayerInterpolate& rPlayer = rFrame.interpolate.player;

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

		// Skip blaster firing if spaceship not visible to player
		if (!FrameInterpolate::IsVisible(rPlayer.vecPosition, rCurrentInterpolate.pVecPositions[i]))
		{
			continue;
		}

		// Fire blasters at player when facing them
		XMVECTOR vecToPlayer = XMVectorSubtract(rPlayer.vecPosition, rCurrentInterpolate.pVecPositions[i]);
		XMVECTOR vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
		float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrentInterpolate.pVecDirections[i], vecToPlayerNormal));

		bool bSpawnBlaster = fAngleToPlayer <= kfSpawnBlasterPlayerAngle;

		if (rCurrentPostRender.pfNextBlasterSpawnTimes[i] < 0.0f && (bSpawnBlaster || rCurrentPostRender.piBlasterSpawns[i] != 2))
		{
			if (rCurrentPostRender.piBlasterSpawns[i] == 1 || rCurrentPostRender.piBlasterSpawns[i] == 2)
			{
				--rCurrentPostRender.piBlasterSpawns[i];
				rCurrentPostRender.pfNextBlasterSpawnTimes[i] = kfBlastersSpawnInterval;

				// Spawn blaster
				XMVECTOR vecDirection = rCurrentInterpolate.pVecDirections[i];
				XMVECTOR vecBlasterVelocity = XMVectorScale(vecDirection, kfBlastersSpeed);
				XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

				BlastersPostRender::Spawn(rFrame,
				{
					.vecPosition = vecPosition,
					.vecVelocity = vecBlasterVelocity,
					.uiTypeIndex = suiEnemyBlasterTypeIndex,
					.flags = {BlasterFlags::kCollidePlayer},
				});
			}
			else
			{
				rCurrentPostRender.piBlasterSpawns[i] = 2;
				rCurrentPostRender.pfNextBlasterSpawnTimes[i] = kfBlastersSpawnCooldown;
			}
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

	// Create owned pusher
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);

	// Create owned target for missile tracking (also creates its billboard)
	rCurrentInterpolate.puiTargets[iIndex] = {};
	TargetsPostRender::Add(rFrame, rCurrentInterpolate.puiTargets[iIndex], suiSpaceshipTargetTypeIndex);

	// Set target flags (PostRender field, not part of Sync)
	int64_t iTargetIndex = rFrame.interpolate.targets.IdToIndex(rCurrentInterpolate.puiTargets[iIndex]);
	rFrame.postRender.targets.pFlags[iTargetIndex] = {TargetFlags::kDestination, TargetFlags::kTargetIsEnemy};

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = {};
	rCurrentPostRender.pVecVelocities[iIndex] = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
	rCurrentPostRender.pVecDamageDirections[iIndex] = XMVectorZero();
	rCurrentPostRender.pfHealths[iIndex] = kfSpaceshipHealth;
	rCurrentPostRender.pfDestroyedExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfNextBlasterSpawnTimes[iIndex] = 0.0f;
	rCurrentPostRender.piBlasterSpawns[iIndex] = 2;

	// Sync owned objects after Add()
	SyncSpaceship(
		rFrame.interpolate,
		rCurrentInterpolate.puiPushers[iIndex],
		rCurrentInterpolate.puiTargets[iIndex],
		rInfo.vecPosition);
}

static void XM_CALLCONV BeginExplosion(Frame& rFrame, int64_t i, FXMVECTOR vecDamageDirection)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	rCurrentPostRender.pFlags[i] |= kExploding;
	rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;
	rCurrentPostRender.pfDestroyedExplosionTimes[i] = kfDestroyExplosionInterval;

	// Store damage direction for knockback
	rCurrentPostRender.pVecDamageDirections[i] = vecDamageDirection;

	// Remove target so missiles stop tracking
	TargetsPostRender::Remove(rFrame, rCurrentInterpolate.puiTargets[i], {TargetFlags::kDestination});
	rCurrentInterpolate.puiTargets[i] = {};

	// Play explosion audio
	static constexpr float kfPitchMin = 0.75f;
	static constexpr float kfPitchRandom = 0.5f;
	float fPitch = kfPitchMin + common::Random<kfPitchRandom>(rFrame.postRender.randomEngine);
	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], 0.4f, fPitch);

	XMVECTOR vecDirection = XMVector3Normalize(rCurrentPostRender.pVecVelocities[i]);
	SpawnSpaceshipExplosion(rFrame, rCurrentInterpolate.pVecPositions[i], vecDirection, 1.0f);
}

void SpaceshipsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	// Build collision flags - mark exploding spaceships as already collided so they don't absorb hits
	sCollisionFlags.resize(static_cast<size_t>(rCurrentInterpolate.iCount));
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {};
	}

	// Add spaceship layer to Collision
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kSpaceship,
		.uiCollidesWith = CollisionMask::kSpaceship,
		.fUniformRadius = 2.0f,
		.fUniformDamage = kfSpaceshipCollisionDamage,
	});
}

void SpaceshipsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	SpaceshipsInterpolate& rCurrentInterpolate = rFrame.interpolate.spaceships;
	SpaceshipsPostRender& rCurrentPostRender = rFrame.postRender.spaceships;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		if (!common::InsideArea(vecPosition, rFrame.postRender.vecArea)) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i] |= kExploding;
			rCurrentInterpolate.pfDestroyedTimes[i] = 0.0f;

			if (rCurrentInterpolate.puiTargets[i].IsValid())
			{
				TargetsPostRender::Remove(rFrame, rCurrentInterpolate.puiTargets[i], {TargetFlags::kDestination});
				rCurrentInterpolate.puiTargets[i] = {};
			}
			continue;
		}

		// Check collision results - spaceships take damage from player blasters only
		// Note: Missile damage is handled via area damage system in AreaDamage phase
		if (engine::Collision::HasCollision(siCollisionLayerIndex, i))
		{
			const auto* pCollisions = engine::Collision::GetCollisions(siCollisionLayerIndex, i);
			for (const auto& rResult : *pCollisions)
			{
				if (rResult.uiOtherCategory == CollisionCategory::kBlasterPlayer)
				{
					rCurrentPostRender.pfHealths[i] -= rResult.fDamageReceived;

					// Play hit sound
					engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioBlaster793907__cvltiv8r__snaresbycvltiv8r301wavCrc, rCurrentInterpolate.pVecPositions[i], 0.2f);

					// Spawn hit flash effect at collision point
					engine::PointLightsPostRender::AddControlled(rFrame, rFrame.interpolate.fCurrentTime, suiSpaceshipHitFlashControllerTypeIndex, rResult.vecContactPoint, 0.0f);

					if (rCurrentPostRender.pfHealths[i] <= 0.0f)
					{
						XMVECTOR vecDamageDirection = XMVector3Normalize(XMVectorNegate(rResult.vecOtherVelocity));
						BeginExplosion(rFrame, i, vecDamageDirection);
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
		// Skip already exploding spaceships
		if (rCurrentPostRender.pFlags[i] & kExploding)
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
			XMVECTOR vecDamageDirection = XMVector3Normalize(
				XMVectorSubtract(vecClosestSource, rCurrentInterpolate.pVecPositions[i]));
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

		// Skip terrain avoidance if close to player and facing them
		XMVECTOR vecToPlayer = XMVectorSubtract(rFrame.interpolate.player.vecPosition, rCurrentInterpolate.pVecPositions[i]);
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

void SpaceshipsInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	const SpaceshipsInterpolate& rCurrent = rFrameInterpolate.spaceships;
	PROFILE_SET_COUNT(engine::kCpuCounterSpaceships, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		engine::gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		engine::gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationY(0.0f) * XMMatrixRotationZ(XM_PIDIV2);

	auto pLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mDynamicStorageBuffers.at(kCrc)[iCommandBuffer].mpMappedMemory);

	int64_t iSpaceshipsRendered = 0;
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!gpCamera->InVisibleArea(gpCamera->f4RenderVisibleArea, f4Position))
		{
			continue;
		}

		// Sentinel value: 0.0f means explosion finished, skip rendering
		if (rCurrent.pfDestroyedTimes[i] == 0.0f)
		{
			continue;
		}

		float fScale = 0.004f;
		if (rCurrent.pfDestroyedTimes[i] > 0.0f)
		{
			fScale *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.75f);
		}

		XMMATRIX matScaling = XMMatrixScaling(fScale, fScale, fScale);
		XMMATRIX matRoll = XMMatrixRotationX(-kfRoll * rCurrent.pfDeltaRotations[i]);
		XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		XMMATRIX matTransform = matScaling * sMatPreRotate * matRoll * matYaw * matTranslation;

		shaders::GltfLayout& rGltfLayout = pLayouts[iSpaceshipsRendered++];
		rGltfLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

		// Freeze color effect
		float fFreezeColor = std::clamp(rCurrent.pfFreezeTimes[i] / kfFreezeTimeBlaster, 0.0f, 1.0f);
		rGltfLayout.f4ColorAdd = {0.5f * fFreezeColor, 0.25f * fFreezeColor, 0.25f * fFreezeColor, 0.0f};
	}
	PROFILE_SET_COUNT(engine::kCpuCounterSpaceshipsRendered, iSpaceshipsRendered);

	engine::gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iSpaceshipsRendered);
	engine::gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iSpaceshipsRendered);
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
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfFreezeTimes[i], rOther.pfFreezeTimes[i]);
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
		bEqual &= common::BreakOnNotEqual(piBlasterSpawns[i], rOther.piBlasterSpawns[i]);
	}

	return bEqual;
}

} // namespace game
