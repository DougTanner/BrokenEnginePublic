// Note: Not using precompiled header so that this file can be optimized in Debug builds
#include "Pch.h"

#include "Missiles.h"

#include "Frame/FrameStaticData.h"
#include "Data/Audio.h"
#include "Frame/HealthDamage.h"
#include "Profile/ProfileManager.h"
#if defined(BT_CLIENT)
#include "Ui/WrapperBase.h"
#include "Ui/LightingWrappers.h"
#include "Ui/SmokeWrappers.h"
#endif
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Explosions/Explosions.h"
#include "Frame/Collections/Targets/Targets.h"

#if defined(BT_CLIENT)
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


#if defined(BT_CLIENT)
// Missile exhaust (non-lighting)
constexpr float kfExhaustWidth = 0.25f;
constexpr float kfExhaustOffset = -0.45f;
#endif // BT_CLIENT


#if defined(BT_CLIENT)
// Missile trail (non-lighting)
constexpr float kfTrailOffset = -0.9f;
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

#if defined(BT_CLIENT)
// Area light type registration for exhaust
static uint8_t suiPlayerExhaustAreaLightTypeIndex = 0xFF;
static uint8_t suiEnemyExhaustAreaLightTypeIndex = 0xFF;

// Trail type registration for smoke trail
static uint8_t suiSmokeTrailTypeIndex = 0xFF;
#endif // BT_CLIENT

// Explosion type registration
static uint8_t suiMissileExplosionTypeIndex = 0xFF;

#if defined(BT_CLIENT)
// Helper to sync owned objects for a missile
void XM_CALLCONV SyncMissile(FrameInterpolate& rFrameInterpolate, engine::area_lights_t uiAreaLight, engine::smoke_trails_t uiSmokeTrail, engine::sound_t uiSound, FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, GXMVECTOR vecPreviousPosition, MissileFlags_t flags, float fPitch, [[maybe_unused]] float fDeltaRotation, float fExhaustLength)
{
	// Sync area light (exhaust flame) if not exploding
	if (uiAreaLight.IsValid() && !(flags & kExploding))
	{
		float fLength = fExhaustLength;
		float fWidth = kfExhaustWidth;
		if ((rFrameInterpolate.iTick) % 2 == 0)
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

	// Sync trail position
	if (uiSmokeTrail.IsValid())
	{
		float fTrailOffset = kfTrailOffset;
		XMVECTOR vecTrailOffset = XMVectorMultiply(XMVectorReplicate(fTrailOffset), XMVector3Normalize(vecDirection));
		XMVECTOR vecTrailPosition = vecPosition + ((flags & kExploding) ? XMVectorZero() : vecTrailOffset);

		engine::SmokeTrailsInterpolate::Sync(rFrameInterpolate, uiSmokeTrail,
		{
			.vecPosition = vecTrailPosition,
			.fIntensity = gMissileTrailIntensity.Get(),
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
#endif // BT_CLIENT

#if defined(BT_CLIENT)
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
	SyncMissile(rFrame.interpolate, rMissiles.puiAreaLights[iIndex], rMissiles.puiSmokeTrails[iIndex], rPostRender.puiSounds[iIndex], rMissiles.pVecPositions[iIndex], rMissiles.pVecDirections[iIndex], rPostRender.pVecVelocities[iIndex], rMissiles.pVecPositions[iIndex], rPostRender.pFlags[iIndex], rPostRender.pfPitches[iIndex], rPostRender.pfDeltaRotations[iIndex], rPostRender.pfExhaustLengths[iIndex]);
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
#endif // BT_CLIENT

void MissilesInterpolate::AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiAreaLights, rPrevious.puiAreaLights, rCurrent.iCount * sizeof(rCurrent.puiAreaLights[0]));
#endif
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiSmokeTrails, rPrevious.puiSmokeTrails, rCurrent.iCount * sizeof(rCurrent.puiSmokeTrails[0]));
#endif
	}
}

void MissilesInterpolate::Register()
{
#if defined(BT_CLIENT)
	// Player missile exhaust
	engine::AreaLightsInterpolate::RegisterType(suiPlayerExhaustAreaLightTypeIndex,
	{
		.crc = data::kTexturesMissilesBC73pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = gMissileExhaustVisibleIntensity.Get(),
		.fLightingSize = gMissileExhaustLightingArea.Get(),
		.fLightingIntensity = gMissileExhaustLightingIntensity.Get(),
		.pVisibleIntensityWrapper = &gMissileExhaustVisibleIntensity,
		.pLightingSizeWrapper = &gMissileExhaustLightingArea,
		.pLightingIntensityWrapper = &gMissileExhaustLightingIntensity,
	});

	// Enemy missile exhaust
	engine::AreaLightsInterpolate::RegisterType(suiEnemyExhaustAreaLightTypeIndex,
	{
		.crc = data::kTexturesMissilesBC71pngCrc,
		.puiColors = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
		.pf2Texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
		.fVisibleIntensity = gMissileExhaustVisibleIntensity.Get(),
		.fLightingSize = gMissileExhaustLightingArea.Get(),
		.fLightingIntensity = gMissileExhaustLightingIntensity.Get(),
		.pVisibleIntensityWrapper = &gMissileExhaustVisibleIntensity,
		.pLightingSizeWrapper = &gMissileExhaustLightingArea,
		.pLightingIntensityWrapper = &gMissileExhaustLightingIntensity,
	});

	// Missile smoke trail
	engine::SmokeTrailsInterpolate::RegisterType(suiSmokeTrailTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
		.fWidth = kfTrailWidth,
	});
#endif // BT_CLIENT

	// Missile explosion type
	engine::ExplosionsInterpolate::RegisterType(suiMissileExplosionTypeIndex,
	{
#if defined(BT_CLIENT)
		.uiPrimaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex(),
		.uiSecondaryLightControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex(),
		.uiPrimaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex(),
		.uiSecondaryPuffControllerTypeIndex = engine::ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex(),
		.uiTrailTypeIndex = engine::ExplosionsInterpolate::GetTrailTypeIndex(),
		.uiWindRadialControllerTypeIndex = engine::ExplosionsInterpolate::GetWindRadialControllerTypeIndex(),
#endif // BT_CLIENT
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
#if defined(BT_CLIENT)
		std::memcpy(rCurrent.puiSounds, rPrevious.puiSounds, rCurrent.iCount * sizeof(rCurrent.puiSounds[0]));
#endif
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

static void RemoveOwnedObjects([[maybe_unused]] Frame& rFrame, [[maybe_unused]] MissilesInterpolate& rCurrentInterpolate, MissilesPostRender& rCurrentPostRender, int64_t i)
{
#if defined(BT_CLIENT)
	if (rCurrentInterpolate.puiAreaLights[i].IsValid())
	{
		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
	}

	if (rCurrentInterpolate.puiSmokeTrails[i].IsValid())
	{
		engine::SmokeTrailsPostRender::Remove(rFrame, rCurrentInterpolate.puiSmokeTrails[i]);
	}
	if (rCurrentPostRender.puiSounds[i].IsValid())
	{
		engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
	}
#endif // BT_CLIENT

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
}

void MissilesPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

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
#if defined(BT_CLIENT)
				.smokeTrailId = rCurrentInterpolate.puiSmokeTrails[i],
#endif
			},
			.iPushedTick = rFrame.interpolate.iTick,
		};
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		// Heap realloc warning: capacity exceeded during burst transfers. Expected max ~1-2/tick
		// per source frame — anything higher suggests entities are re-flagging kTransfer across
		// iterations, DestroyElement isn't removing them, or there's an unexpected push path.
		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			LOG(kDefault, kError,
				"Missile Transfer capacity hit Tick: {} Source: ({},{}) Index: {} Position: {} Velocity: {} Delta: ({},{}) Alignment: {} SourceCount: {} Pushed: {} Capacity: {}",
				rFrame.interpolate.iTick,
				rStaticData.coord.x, rStaticData.coord.y,
				i,
				common::WbV2(vecPosition, 1),
				common::WbV2(rCurrentPostRender.pVecVelocities[i], 1),
				static_cast<int32_t>(request.iDeltaX), static_cast<int32_t>(request.iDeltaY),
				rCurrentPostRender.pAlignments[i],
				rCurrentInterpolate.iCount,
				rFrame.postRender.transferRequests.size(),
				rFrame.postRender.transferRequests.capacity());
			DEBUG_BREAK();
		}
		common::ValidateVector<true >(request.data.vecPosition);
		common::ValidateVector<false>(request.data.vecDirection);
		common::ValidateVector<false>(request.data.vecVelocity);
		rFrame.postRender.transferRequests.push_back(request);

		RemoveOwnedObjects(rFrame, rCurrentInterpolate, rCurrentPostRender, i);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void MissilesPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kExploding) || rCurrentInterpolate.pfDestroyedTimes[i] > 0.0f) [[likely]]
		{
			continue;
		}

		// Area light and sound may already be removed by Explode()
		RemoveOwnedObjects(rFrame, rCurrentInterpolate, rCurrentPostRender, i);

		engine::DestroyElement(rCurrentInterpolate, rCurrentPostRender, i, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void MissilesPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, const SpawnInfo& rInfo)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	common::ValidateVector<true >(rInfo.vecPosition);
	common::ValidateVector<false>(rInfo.vecDirection);
	common::ValidateVector<false>(rInfo.vecVelocity);
	common::ValidateVector<false>(rInfo.vecStoredDirection);

	engine::GrowPairedCollections(rCurrentInterpolate, rCurrentPostRender, rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	int64_t iIndex = engine::AddElement(rCurrentInterpolate, rCurrentPostRender);

	// Initialize interpolate state
	rCurrentInterpolate.pVecPositions[iIndex] = rInfo.vecPosition;
	rCurrentInterpolate.pVecDirections[iIndex] = rInfo.vecDirection;
	rCurrentPostRender.pFlags[iIndex] = rInfo.flags;
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
#if defined(BT_CLIENT)
	MissilesInterpolate::ClientInit(rFrame, iIndex, rInfo.smokeTrailId);
#endif // BT_CLIENT
}

void MissilesPostRender::Explode([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i, [[maybe_unused]] bool bDirectional)
{
	MissilesInterpolate& rCurrentInterpolate = *rFrame.interpolate.pMissiles;
	MissilesPostRender& rCurrentPostRender = *rFrame.postRender.pMissiles;

	if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
	{
		return;
	}

#if defined(BT_CLIENT)
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
#if defined(BT_CLIENT)
	if (rCurrentInterpolate.puiAreaLights[i].IsValid())
	{
		rFrame.postRender.areaLights.Remove(rFrame, rCurrentInterpolate.puiAreaLights[i]);
		rCurrentInterpolate.puiAreaLights[i] = {};
	}
#endif

	// Remove sound when exploding (missile engine sound stops, replaced by explosion sound)
#if defined(BT_CLIENT)
	if (rCurrentPostRender.puiSounds[i].IsValid())
	{
		engine::SoundsPostRender::Remove(rFrame, rCurrentPostRender.puiSounds[i]);
		rCurrentPostRender.puiSounds[i] = {};
	}
#endif

	SpawnMissileExplosion(rFrame, 1.0f, rCurrentInterpolate.pVecPositions[i], rCurrentPostRender.pVecExplosionDirections[i], rCurrentPostRender.pFlags[i]);

	// Register area damage for the AreaDamage phase
	engine::AreaDamage::Add(
	{
		.vecPosition = rCurrentInterpolate.pVecPositions[i],
		.fRadius = kfMissileDamageRadius,
		.fDamage = kfMissileDamage,
		.uiCategory = CollisionCategory::kMissile,
	});
}

bool MissilesInterpolate::LogDifferences(const MissilesInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("MissilesInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference_Vec("pVecDirections", i, pVecDirections[i], rOther.pVecDirections[i]);
		bEqual &= common::LogDifference<"pfDestroyedTimes">(i, pfDestroyedTimes[i], rOther.pfDestroyedTimes[i]);
	}

	return bEqual;
}

bool MissilesPostRender::LogDifferences(const MissilesPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("MissilesPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference_Vec("pVecVelocities", i, pVecVelocities[i], rOther.pVecVelocities[i]);
		bEqual &= common::LogDifference_Vec("pVecExplosionDirections", i, pVecExplosionDirections[i], rOther.pVecExplosionDirections[i]);
		bEqual &= common::LogDifference_Vec("pVecStoredDirections", i, pVecStoredDirections[i], rOther.pVecStoredDirections[i]);
		bEqual &= common::LogDifference<"puiTargets">(i, puiTargets[i], rOther.puiTargets[i]);
		bEqual &= common::LogDifference<"pfExplosionRadii">(i, pfExplosionRadii[i], rOther.pfExplosionRadii[i]);
		bEqual &= common::LogDifference<"pfTimes">(i, pfTimes[i], rOther.pfTimes[i]);
		bEqual &= common::LogDifference<"pfDeltaRotationDelays">(i, pfDeltaRotationDelays[i], rOther.pfDeltaRotationDelays[i]);
		bEqual &= common::LogDifference<"pfDeltaRotations">(i, pfDeltaRotations[i], rOther.pfDeltaRotations[i]);
		bEqual &= common::LogDifference<"pfExaustDelays">(i, pfExaustDelays[i], rOther.pfExaustDelays[i]);
		bEqual &= common::LogDifference<"pfNextJitter">(i, pfNextJitter[i], rOther.pfNextJitter[i]);
		bEqual &= common::LogDifference<"pfDeltaRotationMax">(i, pfDeltaRotationMax[i], rOther.pfDeltaRotationMax[i]);
		bEqual &= common::LogDifference<"pfAccelerations">(i, pfAccelerations[i], rOther.pfAccelerations[i]);
		bEqual &= common::LogDifference<"pfPitches">(i, pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::LogDifference<"pfExhaustLengths">(i, pfExhaustLengths[i], rOther.pfExhaustLengths[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

} // namespace game
