// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Missiles.h"

#include "Frame/HealthDamage.h"
#include "Profile/ProfileManager.h"
#ifdef BT_CLIENT
#include "Ui/WrapperBase.h"
#endif
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Targets/Targets.h"

#include "Data/Audio.h"
#ifdef BT_CLIENT
#include "Data/Scene.h"
#include "Data/Texture.h"
#endif

namespace engine
{
template struct Collection<game::MissilesInterpolate>;
template struct Collection<game::MissilesPostRender>;
}

namespace game
{

using enum MissileFlags;


#ifdef BT_CLIENT
// Missile exhaust
constexpr float kfExhaustWidth = 0.25f;
constexpr float kfExhaustOffset = -0.45f;
constexpr float kfExhaustVisibleIntensity = 1.0f;
constexpr float kfExhaustLightingArea = 11.0f;
constexpr float kfExhaustLightingIntensity = 12.0f;
#endif


#ifdef BT_CLIENT
// Missile trail
constexpr float kfTrailIntensity = 0.5f;
constexpr float kfTrailOffset = -1.0f;
constexpr float kfTrailOffsetExtra = -0.07f;
constexpr float kfTrailWidth = 0.15f;
#endif

// Missile explosion
constexpr uint32_t kuiMissileExplosionBaseParticleCount = 15;
constexpr uint32_t kuiMissileExplosionParticleColor = 0xFF0000FF;
constexpr float kfMissileExplosionParticleVelocityMin = 1.0f;
constexpr float kfMissileExplosionParticleVelocityRandom = 4.0f;
constexpr float kfMissileExplosionParticleVerticalVelocityMin = -2.0f;
constexpr float kfMissileExplosionParticleVerticalVelocityRandom = 4.0f;
constexpr float kfMissileExplosionParticleIntensityDecay = 2.4f;
constexpr float kfExplosionParticleCount = 15.0f;
constexpr float kfExplosionTrailCountMin = 2.0f;
constexpr float kfExplosionTrailCountRandom = 2.0f;

// Missile sound
constexpr float kfMissileSoundVolume = 0.175f;
constexpr float kfMissileSoundFadeOutTime = 0.04f;
constexpr float kfPitchMin = 0.75f;
constexpr float kfPitchRandom = 0.5f;

// Explosion sound
constexpr float kfExplosionSoundVolume = 0.5f;

// Missile spawn
constexpr float kfDeltaRotationLimitMin = 2.0f;
constexpr float kfDeltaRotationLimitRandom = 2.0f;
constexpr float kfExhaustDelay = 0.01f;

#ifdef BT_CLIENT
// Area light type registration for exhaust
static uint8_t suiPlayerExhaustAreaLightTypeIndex = 0xFF;
static uint8_t suiEnemyExhaustAreaLightTypeIndex = 0xFF;

// Trail type registration for smoke trail
static uint8_t suiSmokeTrailTypeIndex = 0xFF;
#endif

// Explosion type registration
static uint8_t suiMissileExplosionTypeIndex = 0xFF;

#ifdef BT_CLIENT
// Helper to sync owned objects for a missile
void XM_CALLCONV SyncMissile(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight, engine::pusher_t uiPusher, engine::smoke_trails_t uiSmokeTrail,
	engine::sound_t uiSound,
	FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, GXMVECTOR vecPreviousPosition, MissileFlags_t flags, float fPitch, float fDeltaRotation, float fExhaustLength)
{
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
		float fIntensityMultiplier = 0.5f + 0.5f * (fLength - kfMissileExhaustLength) / kfMissileExhaustLengthRandom;

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
	if (uiSmokeTrail.IsValid())
	{
		float fTrailOffset = kfTrailOffset + kfTrailOffsetExtra * std::abs(fDeltaRotation);
		XMVECTOR vecTrailOffset = XMVectorMultiply(XMVectorReplicate(fTrailOffset), XMVector3Normalize(vecDirection));
		XMVECTOR vecTrailPosition = vecPosition + ((flags & kExploding) ? XMVectorZero() : vecTrailOffset);

		engine::SmokeTrailsInterpolate::Sync(rFrameInterpolate, uiSmokeTrail,
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
			.fVolume = kfMissileSoundVolume,
			.fPitch = fPitch,
			.fFadeOutTime = kfMissileSoundFadeOutTime,
		});
	}
}
#endif

#ifdef BT_CLIENT
void MissilesInterpolate::ClientInit(Frame& rFrame, int64_t iIndex, engine::smoke_trails_t smokeTrailReuseId)
{
	MissilesInterpolate& rMissiles = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rPostRender = *rFrame.postRender.pMissiles;

	// Add client-only owned objects
	uint8_t uiAreaLightType = (rPostRender.pFlags[iIndex] & kTargetEnemy)
		? suiPlayerExhaustAreaLightTypeIndex : suiEnemyExhaustAreaLightTypeIndex;
	rMissiles.puiAreaLights[iIndex] = {};
	rFrame.postRender.areaLights.Add(rFrame, rMissiles.puiAreaLights[iIndex], uiAreaLightType);

	rMissiles.puiSmokeTrails[iIndex] = {};
	engine::SmokeTrailsPostRender::Add(rFrame, rMissiles.puiSmokeTrails[iIndex], suiSmokeTrailTypeIndex, smokeTrailReuseId);

	rPostRender.puiSounds[iIndex] = {};
	engine::SoundsPostRender::Add(rFrame, rPostRender.puiSounds[iIndex]);

	// Sync all owned objects (reads fields from arrays)
	SyncMissile(rFrame.interpolate, rMissiles.puiAreaLights[iIndex], rMissiles.puiPushers[iIndex], rMissiles.puiSmokeTrails[iIndex],
		rPostRender.puiSounds[iIndex],
		rMissiles.pVecPositions[iIndex], rMissiles.pVecDirections[iIndex], rPostRender.pVecVelocities[iIndex], rMissiles.pVecPositions[iIndex],
		rPostRender.pFlags[iIndex], rPostRender.pfPitches[iIndex], rPostRender.pfDeltaRotations[iIndex], rPostRender.pfExhaustLengths[iIndex]);
}

void MissilesInterpolate::ClientInitAll(Frame& rFrame)
{
	MissilesInterpolate& rMissiles = *rFrame.interpolate.pMissiles;
	for (int64_t i = 0; i < rMissiles.iCount; ++i)
	{
		if (rMissiles.pfDestroyedTimes[i] >= 0.0f)
		{
			continue;
		}
		ClientInit(rFrame, i);
	}
}
#endif

void MissilesInterpolate::AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
#ifdef BT_CLIENT
		std::memcpy(rCurrent.puiAreaLights, rPrevious.puiAreaLights, rCurrent.iCount * sizeof(rCurrent.puiAreaLights[0]));
#endif
		std::memcpy(rCurrent.puiPushers, rPrevious.puiPushers, rCurrent.iCount * sizeof(rCurrent.puiPushers[0]));
#ifdef BT_CLIENT
		std::memcpy(rCurrent.puiSmokeTrails, rPrevious.puiSmokeTrails, rCurrent.iCount * sizeof(rCurrent.puiSmokeTrails[0]));
#endif
	}
}

void MissilesInterpolate::Register()
{
#ifdef BT_CLIENT
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
	engine::SmokeTrailsInterpolate::RegisterType(suiSmokeTrailTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
		.fWidth = kfTrailWidth,
	});
#endif

	// Missile explosion type
	engine::ExplosionsInterpolate::RegisterType(suiMissileExplosionTypeIndex,
	{
#ifdef BT_CLIENT
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
#endif
		.uiBaseParticleCount = kuiMissileExplosionBaseParticleCount,
		.uiParticleColor = kuiMissileExplosionParticleColor,
		.fParticleVelocityMin = kfMissileExplosionParticleVelocityMin,
		.fParticleVelocityRandom = kfMissileExplosionParticleVelocityRandom,
		.fParticleVerticalVelocityMin = kfMissileExplosionParticleVerticalVelocityMin,
		.fParticleVerticalVelocityRandom = kfMissileExplosionParticleVerticalVelocityRandom,
		.fParticleIntensityDecay = kfMissileExplosionParticleIntensityDecay,
		.fSecondaryPositionJitter = 0.8f,
	});
}

static void SpawnMissileExplosion(Frame& __restrict rFrame, float fPercent, XMVECTOR vecPosition, XMVECTOR vecDirection, MissileFlags_t flags)
{
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
#ifdef BT_CLIENT
		std::memcpy(rCurrent.puiSounds, rPrevious.puiSounds, rCurrent.iCount * sizeof(rCurrent.puiSounds[0]));
#endif
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void MissilesPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

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
			.eType = StatusChangeType::kTransferMissile,
			.data = {
				.vecPosition = vecPosition,
				.vecDirection = rCurrentInterpolate.pVecDirections[i],
				.vecVelocity = rCurrentPostRender.pVecVelocities[i],
				.alignment = rCurrentPostRender.pAlignments[i],
				.fAcceleration = rCurrentPostRender.pfAccelerations[i],
				.fDeltaRotationDelay = rCurrentPostRender.pfDeltaRotationDelays[i],
				.fTime = rCurrentPostRender.pfTimes[i],
				.fExhaustDelay = rCurrentPostRender.pfExaustDelays[i],
				.fNextJitter = rCurrentPostRender.pfNextJitter[i],
#ifdef BT_CLIENT
				.smokeTrailId = rCurrentInterpolate.puiSmokeTrails[i],
#endif
			},
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			DEBUG_BREAK();
		}
		rFrame.postRender.transferRequests.push_back(request);

		// Remove owned objects
#ifdef BT_CLIENT
		if (rCurrentInterpolate.puiAreaLights[i].IsValid())
		{
			rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		}
#endif
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
#ifdef BT_CLIENT
		if (rCurrentInterpolate.puiSmokeTrails[i].IsValid())
		{
			engine::SmokeTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiSmokeTrails[i]);
		}
		if (rCurrentPostRender.puiSounds[i].IsValid())
		{
			engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
		}
#endif

		// Remove target subscription (if target still exists)
		if (rCurrentPostRender.puiTargets[i].IsValid())
		{
			const TargetsInterpolate& rTargets = *rFrame.interpolate.pTargets;
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

void MissilesPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kExploding) || rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		// Remove owned objects
#ifdef BT_CLIENT
		if (rCurrentInterpolate.puiAreaLights[i].IsValid())
		{
			rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		}
#endif
		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
#ifdef BT_CLIENT
		if (rCurrentInterpolate.puiSmokeTrails[i].IsValid())
		{
			engine::SmokeTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiSmokeTrails[i]);
		}
		if (rCurrentPostRender.puiSounds[i].IsValid())
		{
			engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
		}
#endif

		// Remove target subscription (if target still exists)
		if (rCurrentPostRender.puiTargets[i].IsValid())
		{
			const TargetsInterpolate& rTargets = *rFrame.interpolate.pTargets;
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
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	// Spawn staggered explosions during death animation - handled in Update
	for (int64_t i = 0; i < rCurrentInterpolate.iCount; ++i)
	{
		[[maybe_unused]] MissileFlags_t flags = rCurrentPostRender.pFlags[i];
	}
}

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
	rCurrentInterpolate.puiPushers[iIndex] = {};
	engine::PushersPostRender::Add(rFrame, rCurrentInterpolate.puiPushers[iIndex]);
	rCurrentInterpolate.pfDestroyedTimes[iIndex] = -1.0f; // Sentinel: -1.0f = not exploding

	// Initialize post-render state
	rCurrentPostRender.pVecVelocities[iIndex] = rInfo.vecVelocity;
	rCurrentPostRender.pVecExplosionDirections[iIndex] = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentPostRender.pVecStoredDirections[iIndex] = rInfo.vecStoredDirection;
	rCurrentPostRender.puiTargets[iIndex] = rInfo.uiTarget;
	rCurrentPostRender.pfExplosionRadii[iIndex] = 0.0f;
	rCurrentPostRender.pfTimes[iIndex] = rInfo.fTime;
	rCurrentPostRender.pfDeltaRotationDelays[iIndex] = rInfo.fDeltaRotationDelay > 0.0f
		? rInfo.fDeltaRotationDelay
		: 0.5f * kfMissileDeltaRotationDelay + common::Random<kfMissileDeltaRotationDelay>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfDeltaRotations[iIndex] = 0.0f;
	rCurrentPostRender.pfExaustDelays[iIndex] = rInfo.fExhaustDelay > 0.0f
		? rInfo.fExhaustDelay
		: kfExhaustDelay;
	float fExhaustLength = kfMissileExhaustLength + common::Random<kfMissileExhaustLengthRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfExhaustLengths[iIndex] = fExhaustLength;
	rCurrentPostRender.pfNextJitter[iIndex] = rInfo.fNextJitter;
	rCurrentPostRender.pfDeltaRotationMax[iIndex] = kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfAccelerations[iIndex] = rInfo.fAcceleration;

	// Create sound with random pitch variation
	float fPitch = kfPitchMin + common::Random<kfPitchRandom>(rFrame.postRender.randomEngine);
	rCurrentPostRender.pfPitches[iIndex] = fPitch;
	rCurrentPostRender.pAlignments[iIndex] = rInfo.alignment;

	// Sync owned objects after Add()
#ifdef BT_CLIENT
	MissilesInterpolate::ClientInit(rFrame, iIndex, rInfo.smokeTrailId);
#else
	engine::PushersInterpolate::Sync(rFrame.interpolate, rCurrentInterpolate.puiPushers[iIndex],
	{
		.vecPosition = rInfo.vecPosition,
		.fRadius = kfMissilePusherRadius,
		.fIntensity = kfMissilePusherIntensity,
		.fPower = kfMissilePusherPower,
		.flags = {engine::PusherFlags::kTypeDefault},
	});
#endif
}

void MissilesPostRender::Explode([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i, [[maybe_unused]] bool bDirectional)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
	{
		return;
	}

#ifdef BT_CLIENT
	engine::gpAudioManager->PlayOneShot3d(rFrame, data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrentInterpolate.pVecPositions[i], kfExplosionSoundVolume);
#endif

	rCurrentPostRender.pFlags[i].Set(kExploding);
	if (bDirectional)
	{
		rCurrentPostRender.pFlags[i].Set(kDirectional);
	}
	rCurrentPostRender.pVecExplosionDirections[i] = bDirectional ? engine::gpIslandTerrain->GlobalNormal(rCurrentInterpolate.pVecPositions[i]) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	rCurrentInterpolate.pfDestroyedTimes[i] = kfMissileDestroyTime;

	// Remove area light when exploding
#ifdef BT_CLIENT
	rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
	rCurrentInterpolate.puiAreaLights[i] = {};
#endif

	// Remove sound when exploding (missile engine sound stops, replaced by explosion sound)
#ifdef BT_CLIENT
	engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
#endif

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
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(puiAreaLights[i], rOther.puiAreaLights[i]);
#endif
		bEqual &= common::BreakOnNotEqual(puiPushers[i], rOther.puiPushers[i]);
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(puiSmokeTrails[i], rOther.puiSmokeTrails[i]);
#endif
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
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(puiSounds[i], rOther.puiSounds[i]);
#endif
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

bool MissilesInterpolate::ServerCompare(const MissilesInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	if (iCount != rOther.iCount)
		FILE_LOG(0, "[ServerCompare] MissilesInterpolate count: client={} server={}", iCount, rOther.iCount);

	for (int64_t i = 0; i < iCount; ++i)
	{
		{
			bool bPos = common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
			if (!bPos) FILE_LOG(0, "[ServerCompare] MissilesInterpolate pos: i={}/{} client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i, iCount, XMVectorGetX(pVecPositions[i]), XMVectorGetY(pVecPositions[i]), XMVectorGetZ(pVecPositions[i]), XMVectorGetX(rOther.pVecPositions[i]), XMVectorGetY(rOther.pVecPositions[i]), XMVectorGetZ(rOther.pVecPositions[i]));
			bEqual &= bPos;
		}
		{
			bool bDir = common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);
			if (!bDir) FILE_LOG(0, "[ServerCompare] MissilesInterpolate dir: i={}/{} client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i, iCount, XMVectorGetX(pVecDirections[i]), XMVectorGetY(pVecDirections[i]), XMVectorGetZ(pVecDirections[i]), XMVectorGetX(rOther.pVecDirections[i]), XMVectorGetY(rOther.pVecDirections[i]), XMVectorGetZ(rOther.pVecDirections[i]));
			bEqual &= bDir;
		}
		bEqual &= common::BreakOnNotEqual(puiPushers[i], rOther.puiPushers[i]);
		bEqual &= common::BreakOnNotEqual(pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
	}

	return bEqual;
}

bool MissilesPostRender::ServerCompare(const MissilesPostRender& rOther) const
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
		bEqual &= common::BreakOnNotEqual(pfNextJitter[i], rOther.pfNextJitter[i]);
		bEqual &= common::BreakOnNotEqual(pfDeltaRotationMax[i], rOther.pfDeltaRotationMax[i]);
		bEqual &= common::BreakOnNotEqual(pfAccelerations[i], rOther.pfAccelerations[i]);
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(pfExhaustLengths[i], rOther.pfExhaustLengths[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

} // namespace game
