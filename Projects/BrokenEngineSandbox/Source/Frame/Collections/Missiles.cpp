// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Missiles.h"

#include "Audio/AudioManager.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Frame/Render.h"
#include "Graphics/Camera.h"
#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/Targets.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"

#include "Data/Audio.h"
#include "Data/Scene.h"
#include "Data/Texture.h"

namespace game
{

constexpr common::crc_t kMissileModelCrc = data::kModelsaim9_missilescenegltfCrc;

using enum MissileFlags;

// Collision layer index (set each frame in PreCollision)
static inline size_t suiCollisionLayerIndex = 0;
static inline std::vector<engine::CollisionFlags_t> sCollisionFlags;
static inline std::vector<float> sCollisionRadii;
static inline std::vector<float> sCollisionDamages;

// File-scope constants (used by multiple functions)
constexpr float kfDeltaRotationDelay = 0.5f;
constexpr float kfExhaustLength = 1.25f;
constexpr float kfExhaustLengthRandom = 1.0f;
constexpr float kfDestroyTime = 0.35f;

// Area light type registration for exhaust
static uint8_t suiPlayerExhaustAreaLightTypeIndex = 0xFF;
static uint8_t suiEnemyExhaustAreaLightTypeIndex = 0xFF;

// Trail type registration for smoke trail
static uint8_t suiTrailTypeIndex = 0xFF;

// Explosion type registration
static uint8_t suiMissileExplosionTypeIndex = 0xFF;

// Helper to sync owned objects for a missile
static void XM_CALLCONV SyncMissile(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight, engine::pusher_t uiPusher, engine::trails_t uiTrail, engine::sound_t uiSound, FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, GXMVECTOR vecPreviousPosition, MissileFlags_t flags, float fPitch, float fDeltaRotation, float fExhaustLength)
{
	static constexpr float kfExhaustWidth = 0.25f;
	static constexpr float kfExhaustOffset = -0.45f;
	static constexpr float kfMissilePusherRadius = 2.0f;
	static constexpr float kfMissilePusherIntensity = 100.0f;
	static constexpr float kfMissilePusherPower = 1.0f;
	static constexpr float kfTrailIntensity = 0.5f;
	static constexpr float kfTrailOffset = -1.0f;
	static constexpr float kfTrailOffsetExtra = -0.07f;

	// Sync area light (exhaust flame) if not exploding
	if (uiAreaLight.IsValid() && !(flags & kExploding))
	{
		float fLength = fExhaustLength;
		float fWidth = kfExhaustWidth;
		if ((rFrameInterpolate.iFrame) % 2 == 0)
		{
			fWidth = -fWidth;
		}

		XMVECTOR vecExhaustOffset = XMVectorMultiply(XMVectorReplicate(kfExhaustOffset), XMVector3Normalize(vecDirection));
		XMVECTOR vecExhaustDirection = XMVector3Normalize(XMVectorAdd(vecDirection, XMVector3Normalize(XMVectorSubtract(vecPosition, vecPreviousPosition))));
		auto [vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight] = common::CalculateArea(XMVectorAdd(vecPosition, vecExhaustOffset), vecExhaustDirection, 0.0f, fLength, fWidth);

		// Intensity varies from 50% at min length to 100% at max length
		float fIntensityMultiplier = 0.5f + 0.5f * (fLength - kfExhaustLength) / kfExhaustLengthRandom;

		engine::AreaLightsInterpolate::Sync(rFrameInterpolate, uiAreaLight,
		{
			.uiTypeIndex = (flags & kTargetPlayer) ? suiEnemyExhaustAreaLightTypeIndex : suiPlayerExhaustAreaLightTypeIndex,
			.vecVisiblePositions = {vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight},
			.fIntensityMultiplier = fIntensityMultiplier,
		});
	}

	// Sync pusher
	engine::PushersInterpolate::Sync(rFrameInterpolate, uiPusher,
	{
		.vecPosition = vecPosition,
		.fRadius = kfMissilePusherRadius,
		.fIntensity = kfMissilePusherIntensity,
		.fPower = kfMissilePusherPower,
		.flags = {engine::PusherFlags::kTypeDefault},
	});

	// Sync trail position
	if (uiTrail.IsValid())
	{
		float fTrailOffset = kfTrailOffset + kfTrailOffsetExtra * std::abs(fDeltaRotation);
		XMVECTOR vecTrailOffset = XMVectorMultiply(XMVectorReplicate(fTrailOffset), XMVector3Normalize(vecDirection));
		XMVECTOR vecTrailPosition = vecPosition + ((flags & kExploding) ? XMVectorZero() : vecTrailOffset);

		engine::TrailsInterpolate::Sync(rFrameInterpolate, uiTrail,
		{
			.vecPosition = vecTrailPosition,
			.fIntensity = kfTrailIntensity,
		});
	}

	// Sync sound position
	if (uiSound.IsValid() && !(flags & kExploding))
	{
		engine::SoundsInterpolate::Sync(rFrameInterpolate, uiSound,
		{
			.vecPosition = vecPosition,
			.vecVelocity = vecVelocity,
			.uiCrc = data::kAudioMissile182794__qubodup__rocketlaunchwavCrc,
			.fVolume = 0.175f,
			.fPitch = fPitch,
			.fFadeOutTime = 0.04f,
		});
	}
}

void MissilesInterpolate::AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiAreaLights, rPrevious.puiAreaLights, rCurrent.iCount * sizeof(rCurrent.puiAreaLights[0]));
		std::memcpy(rCurrent.puiPushers, rPrevious.puiPushers, rCurrent.iCount * sizeof(rCurrent.puiPushers[0]));
		std::memcpy(rCurrent.puiTrails, rPrevious.puiTrails, rCurrent.iCount * sizeof(rCurrent.puiTrails[0]));
		std::memcpy(rCurrent.puiWindTrails, rPrevious.puiWindTrails, rCurrent.iCount * sizeof(rCurrent.puiWindTrails[0]));
	}
}

void MissilesInterpolate::Register()
{
	static constexpr float kfExhaustVisibleIntensity = 1.0f;
	static constexpr float kfExhaustLightingArea = 11.0f;
	static constexpr float kfExhaustLightingIntensity = 12.0f;
	static constexpr float kfTrailWidth = 0.15f;

	// Player missile exhaust
	engine::AreaLightsInterpolate::RegisterType(suiPlayerExhaustAreaLightTypeIndex,
	{
		.crc = data::kTexturesMissilesBC73pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = kfExhaustVisibleIntensity,
		.fLightingSize = kfExhaustLightingArea,
		.fLightingIntensity = kfExhaustLightingIntensity,
	});

	// Enemy missile exhaust
	engine::AreaLightsInterpolate::RegisterType(suiEnemyExhaustAreaLightTypeIndex,
	{
		.crc = data::kTexturesMissilesBC71pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = kfExhaustVisibleIntensity,
		.fLightingSize = kfExhaustLightingArea,
		.fLightingIntensity = kfExhaustLightingIntensity,
	});

	// Missile smoke trail
	engine::TrailsInterpolate::RegisterType(suiTrailTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
		.fWidth = kfTrailWidth,
	});

	// Missile explosion type
	engine::ExplosionsInterpolate::RegisterType(suiMissileExplosionTypeIndex,
	{
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
		.uiBaseParticleCount = 15,
		.uiParticleColor = 0xFF0000FF,
		.fParticleVelocityMin = 5.0f,
		.fParticleVelocityRandom = 15.0f,
	});
}

void MissilesInterpolate::GraphicsResources()
{
	AllocatePipelines(kMissileModelCrc);
}

static void SpawnMissileExplosion(Frame& __restrict rFrame, float fPercent, XMVECTOR vecPosition, XMVECTOR vecDirection, MissileFlags_t flags)
{
	static constexpr float kfExplosionParticleCount = 15.0f;
	static constexpr float kfExplosionTrailCountMin = 2.0f;
	static constexpr float kfExplosionTrailCountRandom = 2.0f;

	// Spawn three simultaneous explosions: full size, half size, quarter size
	// Primary explosion at exact position, secondary explosions with small jitter
	static constexpr float kfSizeMultipliers[] = {1.0f, 0.5f, 0.25f,};
	for (int64_t j = 0; j < 3; ++j)
	{
		float fSizeMultiplier = kfSizeMultipliers[j];
		float fScaledPercent = fPercent * fSizeMultiplier;
		XMVECTOR vecExplosionPosition = vecPosition;
		if (j > 0)
		{
			vecExplosionPosition = common::RandomPositionJitter<0.2f>(vecPosition, rFrame.postRender.randomEngine);
		}

		engine::ExplosionsPostRender::Spawn(rFrame, rFrame.interpolate.fCurrentTime,
			{
				.uiTypeIndex = suiMissileExplosionTypeIndex,
				.vecPosition = vecExplosionPosition,
				.vecDirection = vecDirection,
				.flags = {engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kYellow},
				.uiTrailCount = static_cast<uint32_t>(2.0f * fScaledPercent * (flags & kDirectional ? 0.6f : 1.0f) * kfExplosionTrailCountMin + kfExplosionTrailCountRandom * common::Random(rFrame.postRender.randomEngine)),
				.fTrailAngle = flags & kDirectional ? XM_PI : XM_2PI,
				.uiParticleCount = static_cast<uint32_t>(2.0f * fScaledPercent * kfExplosionParticleCount),
				.fParticleAngle = flags & kDirectional ? XM_PI : XM_2PI,
				.fLightPercent = fScaledPercent,
				.fSizePercent = 0.5f + 2.0f * fScaledPercent,
				.fSmokePercent = flags & kDirectional ? fScaledPercent : 0.5f * fScaledPercent,
				.fTimePercent = fScaledPercent,
			});
	}
}

void MissilesInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	MissilesInterpolate& rCurrent = rCurrentFrameInterpolate.missiles;
	const MissilesInterpolate& rPrevious = rPreviousFrame.interpolate.missiles;
	const MissilesPostRender& rPreviousPostRender = rPreviousFrame.postRender.missiles;
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
		SyncMissile(rCurrentFrameInterpolate, rCurrent.puiAreaLights[i], rCurrent.puiPushers[i], rCurrent.puiTrails[i], rPreviousPostRender.puiSounds[i], vecPosition, vecDirection, rPreviousPostRender.pVecVelocities[i], vecPreviousPosition, flags, rPreviousPostRender.pfPitches[i], rPreviousPostRender.pfDeltaRotations[i], rPreviousPostRender.pfExhaustLengths[i]);

		// Sync wind deposit
		if (rCurrent.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsInterpolate::Sync(rCurrentFrameInterpolate, rCurrent.puiWindTrails[i],
			{
				.vecPosition = vecPosition,
				.fIntensity = engine::gWindDepositIntensity.Get(),
				.fWidth = engine::gWindDepositWidth.Get(),
				.fLengthMultiplier = engine::gWindDepositLengthMultiplier.Get(),
			}, false);
		}
	}
}

void MissilesPostRender::AllocateAndCopy(MissilesPostRender& rCurrent, const MissilesPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Static fields - memcpy (never modified in Update)
	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
		std::memcpy(rCurrent.pVecExplosionDirections, rPrevious.pVecExplosionDirections, rCurrent.iCount * sizeof(rCurrent.pVecExplosionDirections[0]));
		std::memcpy(rCurrent.pfExplosionRadii, rPrevious.pfExplosionRadii, rCurrent.iCount * sizeof(rCurrent.pfExplosionRadii[0]));
		std::memcpy(rCurrent.pfDeltaRotationMax, rPrevious.pfDeltaRotationMax, rCurrent.iCount * sizeof(rCurrent.pfDeltaRotationMax[0]));
		std::memcpy(rCurrent.pfAccelerations, rPrevious.pfAccelerations, rCurrent.iCount * sizeof(rCurrent.pfAccelerations[0]));
		std::memcpy(rCurrent.pfPitches, rPrevious.pfPitches, rCurrent.iCount * sizeof(rCurrent.pfPitches[0]));
		std::memcpy(rCurrent.puiSounds, rPrevious.puiSounds, rCurrent.iCount * sizeof(rCurrent.puiSounds[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void MissilesPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	static constexpr float kfAccelerationAtMaxDeltaAngle = 0.9f;
	static constexpr float kfVelocityDecay = 1.0f;
	static constexpr float kfVelocityToDirection = 16.0f;
	static constexpr float kfJitterIntervalRandom = 0.0025f;
	static constexpr float kfDirectionJitterRandom = 0.06f;
	static constexpr float kfDeltaAngleJitterRandom = 0.5f;
	static constexpr float kfDeltaAngleJitterRandomWithTarget = 1.0f;
	static constexpr float kfDeltaRotationChange = 0.925f;
	static constexpr float kfDeltaRotationDecay = 8.0f;
	static constexpr float kfDeltaRotationTowardsTarget = 10.0f;
	static constexpr float kfDeltaRotationTowardsStored = 3.0f;

	MissilesPostRender& __restrict rCurrent = rFrame.postRender.missiles;
	const MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	const MissilesPostRender& rPrevious = rPreviousFrame.postRender.missiles;
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

				uint32_t uiRandom = common::Random(2, rFrame.postRender.randomEngine);
				float fDeltaAnglePercentExtra = 1.0f + 3.0f * fDeltaAnglePercent;
				float fDeltaAngleJitter = !uiTarget.IsValid() ? kfDeltaAngleJitterRandom : kfDeltaAngleJitterRandomWithTarget;
				if (uiRandom == 0) { vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fDeltaAnglePercentExtra * (-kfDirectionJitterRandom + common::Random<2.0f * kfDirectionJitterRandom>(rFrame.postRender.randomEngine)))); }
				if (uiRandom == 1) { fDeltaRotation += fDeltaAnglePercentExtra * (-fDeltaAngleJitter + fDeltaAngleJitter * common::Random<2.0f>(rFrame.postRender.randomEngine)); }
			}

			// Generate new random exhaust length every frame
			fExhaustLength = kfExhaustLength + common::Random<kfExhaustLengthRandom>(rFrame.postRender.randomEngine);

			// Track target or orient toward stored direction
			if (uiTarget.IsValid())
			{
				const TargetsInterpolate& rTargets = rPreviousFrame.interpolate.targets;

				// Check if target was force-removed (spaceship died)
				if (!rTargets.idToIndexMap.contains(uiTarget))
				{
					// Target no longer exists - clear our reference
					uiTarget = {};
					vecStoredDirection = rCurrentInterpolate.pVecDirections[i];
				}
				else
				{
					const TargetsPostRender& rTargetsPostRender = rPreviousFrame.postRender.targets;
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
						float fDelayPercent = std::clamp(1.0f - fDeltaRotationDelay / kfDeltaRotationDelay, 0.0f, 1.0f);
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
				float fDelayPercent = std::clamp(1.0f - fDeltaRotationDelay / kfDeltaRotationDelay, 0.0f, 1.0f);
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

void MissilesPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	ScopedSuppressAllocationTracking suppressTracking;

	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

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
		sCollisionFlags[static_cast<size_t>(i)] = (rCurrentPostRender.pFlags[i] & kExploding) ? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided} : engine::CollisionFlags_t {engine::CollisionFlags::kDestroyOnCollide};
		sCollisionRadii[static_cast<size_t>(i)] = kfMissileCollisionRadius;
		sCollisionDamages[static_cast<size_t>(i)] = 0.0f;  // Damage via area damage system
	}

	// Note: Damage is applied via area damage system, not direct collision
	suiCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pfRadii = sCollisionRadii.data(),
		.pfDamages = sCollisionDamages.data(),
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = CollisionCategory::kMissile,
		.uiCollidesWith = CollisidesWith::kMissile,
		.pAlignments = rCurrentPostRender.pAlignments,
	});
}

void MissilesPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	if (rCurrentInterpolate.iCount == 0)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		if (!common::InsideArea(vecPosition, rFrame.postRender.vecArea)) [[unlikely]]
		{
			rCurrentPostRender.pFlags[i].Set(kDestroy);
			continue;
		}

		// Check collision results - missiles explode on hit
		if (engine::Collision::HasCollision(suiCollisionLayerIndex, i))
		{
			Explode(rFrame, i, false);
			continue;
		}

		// Collide terrain
		float fElevationFinal = engine::gpIslands->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
		if (XMVectorGetZ(rCurrentInterpolate.pVecPositions[i]) <= fElevationFinal)
		{
			Explode(rFrame, i, true);
		}
	}
}

void MissilesPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void MissilesPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		bool bDestroy = rCurrentPostRender.pFlags[i] & kDestroy;
		bDestroy |= (rCurrentPostRender.pFlags[i] & kExploding) && rCurrentInterpolate.pfDestroyedTimes[i] == 0.0f;

		if (!bDestroy) [[likely]]
		{
			continue;
		}

		// Remove owned objects
		if (rCurrentInterpolate.puiAreaLights[i].IsValid())
		{
			rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		}
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
		engine::TrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiTrails[i]);
		if (rCurrentInterpolate.puiWindTrails[i].IsValid())
		{
			engine::WindTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiWindTrails[i]);
		}
		if (rCurrentPostRender.puiSounds[i].IsValid())
		{
			engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
		}

		// Remove target subscription (if target still exists)
		if (rCurrentPostRender.puiTargets[i].IsValid())
		{
			const TargetsInterpolate& rTargets = rFrame.interpolate.targets;
			if (rTargets.idToIndexMap.contains(rCurrentPostRender.puiTargets[i]))
			{
				TargetsPostRender::Remove(rFrame, rCurrentPostRender.puiTargets[i], {});
			}
			else
			{
				rCurrentPostRender.puiTargets[i] = {};
			}
		}

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	// Spawn staggered explosions during death animation - handled in Update
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		[[maybe_unused]] MissileFlags_t flags = rCurrentPostRender.pFlags[i];
	}
}

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	static constexpr float kfDeltaRotationLimitMin = 2.0f;
	static constexpr float kfDeltaRotationLimitRandom = 2.0f;
	static constexpr float kfExhaustDelay = 0.01f;

	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	uint8_t uiAreaLightType = (rInfo.flags & kTargetEnemy) ? suiPlayerExhaustAreaLightTypeIndex : suiEnemyExhaustAreaLightTypeIndex;
	rCurrentInterpolate.puiAreaLights[iIndex] = {};
	rFrame.postRender.areaLights.Add(rFrame, rCurrentInterpolate.puiAreaLights[iIndex], uiAreaLightType);
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);
	rCurrentInterpolate.puiTrails[iIndex] = {};
	engine::TrailsPostRender::Add(rFrame, rCurrentInterpolate.puiTrails[iIndex], suiTrailTypeIndex);
	rCurrentInterpolate.puiWindTrails[iIndex] = {};
	engine::WindTrailsPostRender::Add(rFrame, rCurrentInterpolate.puiWindTrails[iIndex]);
	engine::WindTrailsInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.puiWindTrails[iIndex],
	{
		.vecPosition = rInfo.vecPosition,
		.fIntensity = engine::gWindDepositIntensity.Get(),
		.fWidth = engine::gWindDepositWidth.Get(),
		.fLengthMultiplier = engine::gWindDepositLengthMultiplier.Get(),
	}, true);
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = -1.0f; // Sentinel: -1.0f = not exploding

	// Initialize post-render state
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecExplosionDirections[iIndex] = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentPostRender.pVecStoredDirections[iIndex] = rInfo.vecStoredDirection;
	rCurrentPostRender.puiTargets[iIndex] = rInfo.uiTarget;
	rCurrentPostRender.pfExplosionRadii[iIndex] = 0.0f;
	rCurrentPostRender.pfTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfDeltaRotationDelays[iIndex] = 0.5f * kfDeltaRotationDelay + common::Random<kfDeltaRotationDelay>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfDeltaRotations[iIndex] = 0.0f;
	rCurrentPostRender.pfExaustDelays[iIndex] = kfExhaustDelay;
	float fExhaustLength = kfExhaustLength + common::Random<kfExhaustLengthRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfExhaustLengths[iIndex] = fExhaustLength;
	rCurrentPostRender.pfNextJitter[iIndex] = 0.0f;
	rCurrentPostRender.pfDeltaRotationMax[iIndex] = kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfAccelerations[iIndex] = rInfo.fAcceleration;

	// Create sound with random pitch variation
	static constexpr float kfPitchMin = 0.75f;
	static constexpr float kfPitchRandom = 0.5f;
	float fPitch = kfPitchMin + common::Random<kfPitchRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfPitches[iIndex] = fPitch;
	rCurrentPostRender.puiSounds[iIndex] = {};
	engine::SoundsPostRender::Add(rFrame, rCurrentPostRender.puiSounds[iIndex]);
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;

	// Sync owned objects after Add()
	SyncMissile(rFrame.interpolate, rCurrentInterpolate.puiAreaLights[iIndex], rCurrentInterpolate.puiPushers[iIndex], rCurrentInterpolate.puiTrails[iIndex], rCurrentPostRender.puiSounds[iIndex], rInfo.vecPosition, rInfo.vecDirection, rInfo.vecVelocity, rInfo.vecPosition, rInfo.flags, fPitch, 0.0f, fExhaustLength);
}

void MissilesPostRender::Explode([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i, [[maybe_unused]] bool bDirectional)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
	{
		return;
	}

	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], 0.5f);

	rCurrentPostRender.pFlags[i].Set(kExploding);
	if (bDirectional)
	{
		rCurrentPostRender.pFlags[i].Set(kDirectional);
	}
	rCurrentPostRender.pVecExplosionDirections[i] = bDirectional ? engine::gpIslands->GlobalNormal(rCurrentInterpolate.pVecPositions[i]) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;

	// Remove area light when exploding
	rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
	rCurrentInterpolate.puiAreaLights[i] = {};

	// Remove sound when exploding (missile engine sound stops, replaced by explosion sound)
	engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);

	SpawnMissileExplosion(rFrame, 1.0f, rCurrentInterpolate.pVecPositions[i], rCurrentPostRender.pVecExplosionDirections[i], rCurrentPostRender.pFlags[i]);

	// Register area damage for the AreaDamage phase
	engine::Collision::AddAreaDamage(
	{
		.vecPosition = rCurrentInterpolate.pVecPositions[i],
		.fRadius = kfMissileDamageRadius,
		.fDamage = kfMissileDamage,
		.uiCategory = CollisionCategory::kMissile,
	});
}

bool MissilesInterpolate::operator==(const MissilesInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::BreakOnNotEqual(puiAreaLights[i], rOther.puiAreaLights[i]);
		bEqual &= common::BreakOnNotEqual(puiPushers[i], rOther.puiPushers[i]);
		bEqual &= common::BreakOnNotEqual(puiTrails[i], rOther.puiTrails[i]);
		bEqual &= common::BreakOnNotEqual(puiWindTrails[i], rOther.puiWindTrails[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
	}

	return bEqual;
}

bool MissilesPostRender::operator==(const MissilesPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::BreakOnNotEqual(pVecExplosionDirections[i], rOther.pVecExplosionDirections[i]);
		bEqual &= common::BreakOnNotEqual(pVecStoredDirections[i], rOther.pVecStoredDirections[i]);
		bEqual &= common::BreakOnNotEqual(puiTargets[i], rOther.puiTargets[i]);
		bEqual &= common::BreakOnNotEqual(pfExplosionRadii[i], rOther.pfExplosionRadii[i]);
		bEqual &= common::BreakOnNotEqual(pfTimes[i], rOther.pfTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotationDelays[i], rOther.pfDeltaRotationDelays[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfExaustDelays[i], rOther.pfExaustDelays[i]);
		bEqual &= common::BreakOnNotEqual(pfExhaustLengths[i], rOther.pfExhaustLengths[i]);
		bEqual &= common::BreakOnNotEqual(pfNextJitter[i], rOther.pfNextJitter[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotationMax[i], rOther.pfDeltaRotationMax[i]);
		bEqual &= common::BreakOnNotEqual(pfAccelerations[i], rOther.pfAccelerations[i]);
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(puiSounds[i], rOther.puiSounds[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

void MissilesInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	const MissilesInterpolate& rCurrent = rFrameInterpolate.missiles;
	gpProfileManager->SetCount(game::kCpuCounterMissiles, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	static const XMMATRIX sMatPreMove = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationZ(XM_PIDIV2);
	static constexpr float kfScale = 0.5f;
	static constexpr float kfWidth = 2.0f;

	auto [pLayouts, iBufferCapacity] = engine::gpBufferManager->GetDynamicStorageBuffer<shaders::ModelLayout>(kCrc, engine::kBufferMain, iCommandBuffer);
	ASSERT(rCurrent.iCount <= iBufferCapacity);

	int64_t iMissilesRendered = 0;
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

		float fScale = kfScale;
		if (rCurrent.pfDestroyedTimes[i] > 0.0f)
		{
			fScale *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.5f);
		}

		XMMATRIX matScaling = XMMatrixScaling(kfWidth * fScale, fScale, fScale);
		XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		XMMATRIX matTransform = sMatPreMove * matScaling * sMatPreRotate * matYaw * matTranslation;

		shaders::ModelLayout& rModelLayout = pLayouts[iMissilesRendered++];
		rModelLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rModelLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rModelLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
		rModelLayout.uiMeshDataBase = 0;
	}
	gpProfileManager->SetCount(game::kCpuCounterMissilesRendered, iMissilesRendered);

	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModel].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iMissilesRendered);
	engine::gpPipelineManager->mDynamicModelPipelineMaps[engine::kDynamicModelPipelineModelShadow].at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iMissilesRendered);
}

} // namespace game
