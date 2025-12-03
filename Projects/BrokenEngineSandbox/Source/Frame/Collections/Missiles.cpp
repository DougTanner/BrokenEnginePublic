// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Missiles.h"

#include "Audio/AudioManager.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions.h"
#include "Frame/Collections/Targets.h"
#include "Frame/Collision.h"
#include "Frame/Frame.h"
#include "Frame/HealthDamage.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace game
{

using enum MissileFlags;

// Collision layer (set each frame in PreCollision)
static inline int64_t siCollisionLayerIndex = 0;
static inline std::vector<engine::CollisionFlags_t> sCollisionFlags;

// AI constants
constexpr float kfAccelerationAtMaxDeltaAngle = 0.9f;
constexpr float kfVelocityDecay = 1.0f;
constexpr float kfVelocityToDirection = 16.0f;

constexpr float kfJitterIntervalRandom = 0.0025f;
constexpr float kfDirectionJitterRandom = 0.03f;
constexpr float kfDeltaAngleJitterRandom = 0.4f;
constexpr float kfDeltaAngleJitterRandomWithTarget = 0.8f;
constexpr float kfPositionJitterRandom = 0.01f;

constexpr float kfDeltaRotationDelay = 1.0f;
constexpr float kfDeltaRotationLimitMin = 2.0f;
constexpr float kfDeltaRotationLimitRandom = 2.0f;
constexpr float kfDeltaRotationChange = 0.97f;
constexpr float kfDeltaRotationDecay = 8.0f;
constexpr float kfDeltaRotationTowardsTarget = 6.0f;

// Exhaust visual constants
constexpr float kfExhaustVisibleIntensity = 1.0f;
constexpr float kfExhaustLightingArea = 10.0f;
constexpr float kfExhaustLightingIntensity = 10.0f;
constexpr float kfExhaustLength = 1.0f;
constexpr float kfExhaustLengthRandom = 1.0f;
constexpr float kfExhaustWidth = 0.25f;
constexpr float kfExhaustOffset = -0.5f;
constexpr float kfExhaustDelay = 0.01f;

// Trail constants
constexpr float kfTrailIntensity = 0.5f;
constexpr float kfTrailWidth = 0.15f;
constexpr float kfTrailOffset = -1.0f;
constexpr float kfTrailOffsetExtra = -0.07f;

// Destruction constants
constexpr float kfDestroyTime = 0.35f;
constexpr float kfDestroyExplosionInterval = 0.03f;
constexpr float kfExplosionPositionJitter = 1.4f;
constexpr float kfExplosionParticleCount = 15.0f;
constexpr float kfExplosionTrailCountMin = 2.0f;
constexpr float kfExplosionTrailCountRandom = 2.0f;

// Area light type registration for exhaust
static uint8_t suiPlayerExhaustAreaLightTypeIndex = 0xFF;
static uint8_t suiEnemyExhaustAreaLightTypeIndex = 0xFF;

// Trail type registration for smoke trail
static uint8_t suiTrailTypeIndex = 0xFF;

void MissilesInterpolate::Register()
{
	ASSERT(suiPlayerExhaustAreaLightTypeIndex == 0xFF);
	ASSERT(suiEnemyExhaustAreaLightTypeIndex == 0xFF);
	ASSERT(suiTrailTypeIndex == 0xFF);

	// Player missile exhaust
	suiPlayerExhaustAreaLightTypeIndex = static_cast<uint8_t>(engine::AreaLightsInterpolate::sTypes.size());
	engine::AreaLightsInterpolate::sTypes.push_back(
	{
		.crc = data::kTexturesMissilesBC72pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = kfExhaustVisibleIntensity,
		.fLightingSize = kfExhaustLightingArea,
		.fLightingIntensity = kfExhaustLightingIntensity,
	});

	// Enemy missile exhaust
	suiEnemyExhaustAreaLightTypeIndex = static_cast<uint8_t>(engine::AreaLightsInterpolate::sTypes.size());
	engine::AreaLightsInterpolate::sTypes.push_back(
	{
		.crc = data::kTexturesMissilesBC71pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = kfExhaustVisibleIntensity,
		.fLightingSize = kfExhaustLightingArea,
		.fLightingIntensity = kfExhaustLightingIntensity,
	});

	// Missile smoke trail
	suiTrailTypeIndex = engine::TrailsPostRender::RegisterType(
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
	});
}

// Explosion type registration
static uint8_t suiMissileExplosionTypeIndex = 255;

static uint8_t RegisterMissileExplosionType()
{
	if (suiMissileExplosionTypeIndex == 255)
	{
		// Start with default explosion type that has registered effect indices
		engine::ExplosionType kMissileExplosionType = engine::ExplosionsPostRender::CreateDefaultType();

		// Customize particle settings
		kMissileExplosionType.uiBaseParticleCount = 15;
		kMissileExplosionType.uiParticleColor = 0xFF00FFFF;
		kMissileExplosionType.fParticleVelocityMin = 5.0f;
		kMissileExplosionType.fParticleVelocityRandom = 15.0f;
		kMissileExplosionType.fPusherRadius = 3.0f;
		kMissileExplosionType.fPusherIntensity = 10000.0f;

		suiMissileExplosionTypeIndex = engine::ExplosionsPostRender::RegisterType(kMissileExplosionType);
	}
	return suiMissileExplosionTypeIndex;
}

static void XM_CALLCONV SpawnMissileExplosion(Frame& __restrict rFrame, float fPercent, FXMVECTOR vecPosition, FXMVECTOR vecDirection, MissileFlags_t flags)
{
	static constexpr float kfPositionJitter = 0.5f;
	XMVECTOR vecJitteredPosition = XMVectorAdd(
		XMVectorSet(
			-kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.postRender.randomEngine),
			-kfPositionJitter + common::Random<2.0f * kfPositionJitter>(rFrame.postRender.randomEngine),
			0.0f, 0.0f),
		vecPosition);

	engine::ExplosionsPostRender::Spawn(
		rFrame,
		rFrame.interpolate.fCurrentTime,
		RegisterMissileExplosionType(),
		vecJitteredPosition,
		vecDirection,
		{engine::ExplosionFlags::kDestroysSelf, engine::ExplosionFlags::kYellow},
		static_cast<uint32_t>(fPercent * (flags & kDirectional ? 0.6f : 1.0f) * kfExplosionTrailCountMin + kfExplosionTrailCountRandom * common::Random(rFrame.postRender.randomEngine)),
		flags & kDirectional ? XM_PI : XM_2PI,
		static_cast<uint32_t>(fPercent * kfExplosionParticleCount),
		flags & kDirectional ? XM_PI : XM_2PI,
		fPercent,
		0.0f,
		0.25f + fPercent,
		flags & kDirectional ? fPercent : 0.5f * fPercent,
		fPercent);
}

void MissilesInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	MissilesInterpolate& rCurrent = rCurrentFrameInterpolate.missiles;
	const MissilesInterpolate& rPrevious = rPreviousFrame.interpolate.missiles;
	const MissilesPostRender& rPreviousPostRender = rPreviousFrame.postRender.missiles;
	engine::AreaLightsInterpolate& rAreaLights = rCurrentFrameInterpolate.areaLights;
	engine::TrailsInterpolate& rTrails = rCurrentFrameInterpolate.trails;
	engine::SoundsInterpolate& rSounds = rCurrentFrameInterpolate.sounds;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];
		engine::area_lights_t uiAreaLight = rPrevious.puiAreaLights[i];
		engine::pusher_t uiPusher = rPrevious.puiPushers[i];
		engine::trails_t uiTrail = rPrevious.puiTrails[i];
		float fDestroyedTime = rPrevious.pfDestroyedTimes[i];
		MissileFlags_t flags = rPreviousPostRender.pFlags[i];

		if (!(flags & kExploding)) [[likely]]
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPreviousPostRender.pVecVelocities[i], vecPosition);

			// Add delta rotation to direction
			float fDeltaRotationDelayPercent = std::clamp(1.0f - rPreviousPostRender.pfDeltaRotationDelays[i] / kfDeltaRotationDelay, 0.0f, 1.0f);
			vecDirection = XMVector3Normalize(XMVector4Transform(vecDirection, XMMatrixRotationZ(fDeltaTime * fDeltaRotationDelayPercent * rPreviousPostRender.pfDeltaRotations[i])));
		}

		// Decay destroyed time
		if (fDestroyedTime > 0.0f)
		{
			fDestroyedTime = std::max(fDestroyedTime - fDeltaTime, 0.0f);
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.puiAreaLights[i] = uiAreaLight;
		rCurrent.puiPushers[i] = uiPusher;
		rCurrent.puiTrails[i] = uiTrail;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;

		// Sync area light (exhaust flame) if not exploding and delay has passed
		if (uiAreaLight.IsValid() && !(flags & kExploding) && rPreviousPostRender.pfExaustDelays[i] <= 0.0f)
		{
			float fLength = kfExhaustLength;
			float fWidth = kfExhaustWidth;
			if ((rCurrentFrameInterpolate.iFrame) % 2 == 0)
			{
				fWidth = -fWidth;
			}

			XMVECTOR vecExhaustOffset = XMVectorMultiply(XMVectorReplicate(kfExhaustOffset), XMVector3Normalize(vecDirection));
			XMVECTOR vecExhaustDirection = XMVector3Normalize(XMVectorAdd(vecDirection, XMVector3Normalize(XMVectorSubtract(vecPosition, rPreviousFrame.interpolate.missiles.pVecPositions[i]))));
			auto [vecTopLeft, vecTopRight, vecBottomLeft, vecBottomRight] = common::CalculateArea(XMVectorAdd(vecPosition, vecExhaustOffset), vecExhaustDirection, 0.0f, fLength, fWidth);

			uint64_t uiAreaLightIndex = rAreaLights.IdToIndex(uiAreaLight);
			rAreaLights.puiTypeIndices[uiAreaLightIndex] = (flags & kTargetPlayer) ? suiEnemyExhaustAreaLightTypeIndex : suiPlayerExhaustAreaLightTypeIndex;
			rAreaLights.pVecVisiblePositions[0][uiAreaLightIndex] = vecTopLeft;
			rAreaLights.pVecVisiblePositions[1][uiAreaLightIndex] = vecTopRight;
			rAreaLights.pVecVisiblePositions[2][uiAreaLightIndex] = vecBottomLeft;
			rAreaLights.pVecVisiblePositions[3][uiAreaLightIndex] = vecBottomRight;
		}

		// Sync trail position
		engine::sound_t uiSound = rPreviousPostRender.puiSounds[i];
		if (uiTrail.IsValid())
		{
			float fTrailOffset = kfTrailOffset + kfTrailOffsetExtra * std::abs(rPreviousPostRender.pfDeltaRotations[i]);
			XMVECTOR vecTrailOffset = XMVectorMultiply(XMVectorReplicate(fTrailOffset), XMVector3Normalize(vecDirection));
			XMVECTOR vecTrailPosition = vecPosition + ((flags & kExploding) ? XMVectorZero() : vecTrailOffset);

			uint64_t uiTrailIndex = rTrails.IdToIndex(uiTrail);
			rTrails.pVecPositions[uiTrailIndex] = vecTrailPosition;
			rTrails.pfIntensities[uiTrailIndex] = kfTrailIntensity;
		}

		// Sync sound position
		if (uiSound.IsValid() && !(flags & kExploding))
		{
			uint64_t uiSoundIndex = rSounds.IdToIndex(uiSound);
			rSounds.puiCrcs[uiSoundIndex] = data::kAudioMissile182794__qubodup__rocketlaunchwavCrc;
			rSounds.pfVolumes[uiSoundIndex] = 0.175f;
			rSounds.pfPitches[uiSoundIndex] = rPreviousPostRender.pfPitches[i];
			rSounds.pfFadeOutTimes[uiSoundIndex] = 0.04f;
			rSounds.pVecPositions[uiSoundIndex] = vecPosition;
			rSounds.pVecVelocities[uiSoundIndex] = rPreviousPostRender.pVecVelocities[i];
		}
	}
}

void MissilesPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	MissilesPostRender& rCurrent = rFrame.postRender.missiles;
	const MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	const MissilesPostRender& rPrevious = rPreviousFrame.postRender.missiles;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		MissileFlags_t flags = rPrevious.pFlags[i];
		XMVECTOR vecVelocity = rPrevious.pVecVelocities[i];
		XMVECTOR vecExplosionDirection = rPrevious.pVecExplosionDirections[i];
		target_t uiTarget = rPrevious.puiTargets[i];
		float fExplosionRadius = rPrevious.pfExplosionRadii[i];
		float fTime = rPrevious.pfTimes[i] + fDeltaTime;
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fDeltaRotationDelay = rPrevious.pfDeltaRotationDelays[i];
		float fExaustDelay = rPrevious.pfExaustDelays[i] - fDeltaTime;
		float fNextJitter = rPrevious.pfNextJitter[i] - fDeltaTime;
		float fDeltaRotationMax = rPrevious.pfDeltaRotationMax[i];
		float fExplosionTime = rPrevious.pfExplosionTimes[i];
		float fAcceleration = rPrevious.pfAccelerations[i];
		float fPitch = rPrevious.pfPitches[i];
		engine::sound_t uiSound = rPrevious.puiSounds[i];

		// Update pusher position (must have Frame& access, so done here instead of Sync)
		engine::PushersPostRender::UpdatePosition(rFrame, rFrame.interpolate.missiles.puiPushers[i], rCurrentInterpolate.pVecPositions[i]);

		if (!(flags & kExploding)) [[likely]]
		{
			// Decay velocity
			vecVelocity = XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfVelocityDecay), vecVelocity);

			// Accelerate
			float fDeltaAnglePercent = std::abs(fDeltaRotation) / fDeltaRotationMax;
			fDeltaAnglePercent = std::clamp(fDeltaAnglePercent, 0.0f, 1.0f);
			float fAdjustedAcceleration = (1.0f - fDeltaAnglePercent) * fAcceleration + fDeltaAnglePercent * kfAccelerationAtMaxDeltaAngle * fAcceleration;
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

			// Track target
			if (uiTarget.IsValid())
			{
				fDeltaRotationDelay -= fDeltaTime;

				const TargetsInterpolate& rTargets = rPreviousFrame.interpolate.targets;
				if (rTargets.iCount > 0)
				{
					uint64_t uiTargetIndex = rTargets.IdToIndex(uiTarget);
					if (uiTargetIndex < static_cast<uint64_t>(rTargets.iCount))
					{
						XMVECTOR vecTargetPosition = rTargets.pVecPositions[uiTargetIndex];
						XMVECTOR vecToTargetNormal = XMVector3Normalize(XMVectorSubtract(vecTargetPosition, rCurrentInterpolate.pVecPositions[i]));
						float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecToTargetNormal));
						float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaRotationTowardsTarget : -kfDeltaRotationTowardsTarget;

						fDeltaRotation = kfDeltaRotationChange * fDeltaRotation + (1.0f - kfDeltaRotationChange) * fWantedDeltaRotation;
					}
				}
			}

			// Clamp delta rotation
			fDeltaRotation = common::MinAbs(fDeltaRotation, fDeltaRotationMax);

			// Keep velocity in XY plane
			vecVelocity = XMVectorSetZ(vecVelocity, 0.0f);
		}
		else
		{
			// Spawn destruction explosions
			fExplosionTime = fExplosionTime - fDeltaTime;
			if (fExplosionTime < 0.0f)
			{
				fExplosionTime = kfDestroyExplosionInterval;

				float fPercent = std::max(rCurrentInterpolate.pfDestroyedTimes[i] / kfDestroyTime, 0.1f);
				XMVECTOR vecExplosionPosition = XMVectorAdd(fExplosionRadius * XMVectorSet(-kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.postRender.randomEngine), -kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.postRender.randomEngine), 0.0f, 0.0f), rCurrentInterpolate.pVecPositions[i]);
				SpawnMissileExplosion(rFrame, fPercent, vecExplosionPosition, vecExplosionDirection, flags);
			}
		}

		// Save
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pVecExplosionDirections[i] = vecExplosionDirection;
		rCurrent.puiTargets[i] = uiTarget;
		rCurrent.pfExplosionRadii[i] = fExplosionRadius;
		rCurrent.pfTimes[i] = fTime;
		rCurrent.pfDeltaRotationDelays[i] = fDeltaRotationDelay;
		rCurrent.pfDeltaRotations[i] = fDeltaRotation;
		rCurrent.pfExaustDelays[i] = fExaustDelay;
		rCurrent.pfNextJitter[i] = fNextJitter;
		rCurrent.pfDeltaRotationMax[i] = fDeltaRotationMax;
		rCurrent.pfExplosionTimes[i] = fExplosionTime;
		rCurrent.pfAccelerations[i] = fAcceleration;
		rCurrent.pfPitches[i] = fPitch;
		rCurrent.puiSounds[i] = uiSound;
	}
}

void MissilesPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	// Build collision flags - mark exploding missiles as already collided so they don't absorb hits
	sCollisionFlags.resize(static_cast<size_t>(rCurrentInterpolate.iCount));
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		sCollisionFlags.at(static_cast<size_t>(i)) = (rCurrentPostRender.pFlags[i] & kExploding)
			? engine::CollisionFlags_t {engine::CollisionFlags::kAlreadyCollided}
			: engine::CollisionFlags_t {engine::CollisionFlags::kDestroyOnCollide};
	}

	// Add missile layer to Collision (missiles only hit spaceships, never player)
	siCollisionLayerIndex = engine::Collision::AddLayer(
	{
		.pVecPositions = rCurrentInterpolate.pVecPositions,
		.pFlags = sCollisionFlags.data(),
		.iCount = rCurrentInterpolate.iCount,
		.uiCategory = game::CollisionCategory::kMissile,
		.uiCollidesWith = game::CollisionMask::kMissile,
		.fUniformRadius = kfMissileCollisionRadius,
		.fUniformDamage = kfMissileDamage,
	});
}

void MissilesPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		// Check collision results - missiles explode on hit
		if (engine::Collision::HasCollision(siCollisionLayerIndex, i))
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

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	// Spawn staggered explosions during death animation - handled in Update
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		[[maybe_unused]] MissileFlags_t flags = rCurrentPostRender.pFlags[i];
	}
}

void XM_CALLCONV MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime, FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, target_t uiTarget, float fAcceleration, MissileFlags_t flags)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Interpolate defaults
	rCurrentInterpolate.pVecPositions[iIndex] = vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = vecDirection;
	rCurrentInterpolate.puiAreaLights[iIndex] = rFrame.postRender.areaLights.Add(rFrame, (flags & kTargetPlayer) ? suiEnemyExhaustAreaLightTypeIndex : suiPlayerExhaustAreaLightTypeIndex);
	rCurrentInterpolate.puiPushers[iIndex] = engine::PushersPostRender::Add(rFrame, vecPosition, 1.5f, 400.0f, 5.0f, engine::PusherFlags::kTypeDefault);
	rCurrentInterpolate.puiTrails[iIndex] = engine::TrailsPostRender::Add(rFrame, rFrame.interpolate.fCurrentTime, suiTrailTypeIndex, vecPosition, kfTrailIntensity, kfTrailWidth);
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = -1.0f; // Sentinel: -1.0f = not exploding

	// PostRender defaults
	rCurrentPostRender.pFlags[iIndex] = flags;
	rCurrentPostRender.pVecVelocities[iIndex] = vecVelocity;
	rCurrentPostRender.pVecExplosionDirections[iIndex] = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentPostRender.puiTargets[iIndex] = uiTarget;
	rCurrentPostRender.pfExplosionRadii[iIndex] = 0.0f;
	rCurrentPostRender.pfTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfDeltaRotationDelays[iIndex] = 0.5f * kfDeltaRotationDelay + common::Random<kfDeltaRotationDelay>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfDeltaRotations[iIndex] = 0.0f;
	rCurrentPostRender.pfExaustDelays[iIndex] = kfExhaustDelay;
	rCurrentPostRender.pfNextJitter[iIndex] = 0.0f;
	rCurrentPostRender.pfDeltaRotationMax[iIndex] = kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfExplosionTimes[iIndex] = 0.0f;
	rCurrentPostRender.pfAccelerations[iIndex] = fAcceleration;

	// Create sound with random pitch variation
	static constexpr float kfPitchMin = 0.75f;
	static constexpr float kfPitchRandom = 0.5f;
	float fPitch = kfPitchMin + common::Random<kfPitchRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfPitches[iIndex] = fPitch;
	rCurrentPostRender.puiSounds[iIndex] = engine::SoundsPostRender::Add(rFrame, data::kAudioMissile182794__qubodup__rocketlaunchwavCrc, 0.175f, fPitch, 0.04f, vecPosition, vecVelocity);
}

void MissilesPostRender::Explode([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i, [[maybe_unused]] bool bDirectional)
{
	MissilesInterpolate& rCurrentInterpolate = rFrame.interpolate.missiles;
	MissilesPostRender& rCurrentPostRender = rFrame.postRender.missiles;

	if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
	{
		return;
	}

	engine::gpAudioManager->PlayOneShot3d(data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], 0.7f);

	rCurrentPostRender.pFlags[i] |= kExploding;
	if (bDirectional)
	{
		rCurrentPostRender.pFlags[i] |= kDirectional;
	}
	rCurrentPostRender.pVecExplosionDirections[i] = bDirectional ? engine::gpIslands->GlobalNormal(rCurrentInterpolate.pVecPositions[i]) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentInterpolate.pfDestroyedTimes[i] = kfDestroyTime;
	rCurrentPostRender.pfExplosionTimes[i] = kfDestroyExplosionInterval;

	// Remove area light when exploding
	rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
	rCurrentInterpolate.puiAreaLights[i] = {};

	SpawnMissileExplosion(rFrame, 1.0f, rCurrentInterpolate.pVecPositions[i], rCurrentPostRender.pVecExplosionDirections[i], rCurrentPostRender.pFlags[i]);
}

void MissilesPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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
		engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);

		if (rCurrentInterpolate.iCount - 1 > i) [[likely]]
		{
			engine::SwapElement(rCurrentInterpolate, i, rCurrentInterpolate.Members());
			engine::SwapElement(rCurrentPostRender, i, rCurrentPostRender.Members());
			--i;
		}

		--rCurrentInterpolate.iCount;
		--rCurrentPostRender.iCount;
	}
}

void MissilesInterpolate::Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer)
{
	const MissilesInterpolate& rCurrent = rFrameInterpolate.missiles;
	PROFILE_SET_COUNT(engine::kCpuCounterMissiles, rCurrent.iCount);

	if (rCurrent.iCount == 0)
	{
		engine::gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		engine::gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, 0);
		return;
	}

	ResizeBufferUpdateDescriptor(rCurrent, iCommandBuffer);

	static const XMMATRIX sMatPreMove = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static const XMMATRIX sMatPreRotate = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationZ(XM_PIDIV2);
	static constexpr float kfScale = 0.5f;
	static constexpr float kfWidth = 2.0f;

	auto pLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mDynamicStorageBuffers.at(kCrc)[iCommandBuffer].mpMappedMemory);

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

		XMMATRIX matScaling = XMMatrixScaling(fScale, fScale, kfWidth * fScale);
		XMMATRIX matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		XMMATRIX matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);
		XMMATRIX matTransform = sMatPreMove * matScaling * sMatPreRotate * matYaw * matTranslation;

		shaders::GltfLayout& rGltfLayout = pLayouts[iMissilesRendered++];
		rGltfLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rGltfLayout.f4ColorAdd = {0.0f, 0.0f, 0.0f, 0.0f};
	}
	PROFILE_SET_COUNT(engine::kCpuCounterMissilesRendered, iMissilesRendered);

	engine::gpPipelineManager->mDynamicGltfPipelineMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iMissilesRendered);
	engine::gpPipelineManager->mDynamicGltfPipelineShadowMap.at(kCrc)->WriteIndirectBuffer(iCommandBuffer, iMissilesRendered);
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
		bEqual &= common::BreakOnNotEqual(puiTargets[i], rOther.puiTargets[i]);
		bEqual &= common::BreakOnNotEqual(pfExplosionRadii[i], rOther.pfExplosionRadii[i]);
		bEqual &= common::BreakOnNotEqual(pfTimes[i], rOther.pfTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotationDelays[i], rOther.pfDeltaRotationDelays[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfExaustDelays[i], rOther.pfExaustDelays[i]);
		bEqual &= common::BreakOnNotEqual(pfNextJitter[i], rOther.pfNextJitter[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotationMax[i], rOther.pfDeltaRotationMax[i]);
		bEqual &= common::BreakOnNotEqual(pfExplosionTimes[i], rOther.pfExplosionTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfAccelerations[i], rOther.pfAccelerations[i]);
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(puiSounds[i], rOther.puiSounds[i]);
	}

	return bEqual;
}

} // namespace game
