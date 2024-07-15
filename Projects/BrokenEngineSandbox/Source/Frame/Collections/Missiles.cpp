#include "Missiles.h"

#include "Audio/AudioManager.h"
#include "Frame/Render.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Ui/Wrapper.h"
#include "Frame/Frame.h"

#include "Game.h"
#include "Input/Input.h"

using namespace DirectX;

using enum engine::ExplosionFlags;
using enum engine::TargetFlags;

namespace game
{

using enum MissileFlags;

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

constexpr float kfExhaustVisibleIntensity = 1.0f;
constexpr float kfExhaustLightingArea = 5.0f;
constexpr float kfExhaustLightingIntensity = 600.0f;
constexpr float kfExhaustLength = 1.0f;
constexpr float kfExhaustLengthRandom = 1.0f;
constexpr float kfExhaustWidth = 0.25f;
constexpr float kfExhaustOffset = -0.5f;
constexpr float kfExhaustDelay = 0.01f;

constexpr float kfTrailIntensity = 0.1f;
constexpr float kfTrailOffset = -1.0f;
constexpr float kfTrailOffsetExtra = -0.07f;

constexpr float kfDestroyTime = 0.35f;
constexpr float kfDestroyExplosionInterval = 0.03f;
constexpr float kfExplosionPositionJitter = 1.4f;
constexpr float kfExplosionParticleCount = 15.0f;
constexpr float kfExplosionTrailCountMin = 2.0f;
constexpr float kfExplosionTrailCountRandom = 2.0f;

bool Missiles::operator==(const Missiles& rOther) const
{
	bool bEqual = true;

	bEqual &= *static_cast<const Spawnable*>(this) == *static_cast<const Spawnable*>(&rOther);

	bEqual &= iCount == rOther.iCount;

	common::BreakOnNotEqual(bEqual);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= pVecPositions[i] == rOther.pVecPositions[i];
		bEqual &= pVecDirections[i] == rOther.pVecDirections[i];
		bEqual &= puiAreaLights[i] == rOther.puiAreaLights[i];
		bEqual &= puiPushers[i] == rOther.puiPushers[i];
		bEqual &= puiTrails[i] == rOther.puiTrails[i];
		bEqual &= puiSelfTargets[i] == rOther.puiSelfTargets[i];
		bEqual &= pfDestroyedTimes[i] == rOther.pfDestroyedTimes[i];

		bEqual &= pFlags[i] == rOther.pFlags[i];
		bEqual &= pVecVelocities[i] == rOther.pVecVelocities[i];
		bEqual &= pVecExplosionDirections[i] == rOther.pVecExplosionDirections[i];
		bEqual &= puiTargets[i] == rOther.puiTargets[i];
		bEqual &= pfExplosionRadii[i] == rOther.pfExplosionRadii[i];
		bEqual &= pfTimes[i] == rOther.pfTimes[i];
		bEqual &= pfDeltaRotations[i] == rOther.pfDeltaRotations[i];
		bEqual &= pfDeltaRotationDelays[i] == rOther.pfDeltaRotationDelays[i];
		bEqual &= pfExaustDelays[i] == rOther.pfExaustDelays[i];
		bEqual &= pfNextJitter[i] == rOther.pfNextJitter[i];
		bEqual &= pfDeltaRotationMax[i] == rOther.pfDeltaRotationMax[i];
		bEqual &= pfExplosionTimes[i] == rOther.pfExplosionTimes[i];
		bEqual &= pfAccelerations[i] == rOther.pfAccelerations[i];
		bEqual &= pfPitches[i] == rOther.pfPitches[i];
		bEqual &= puiSounds[i] == rOther.puiSounds[i];

		common::BreakOnNotEqual(bEqual);
	}

	return bEqual;
}

void Missiles::Copy(int64_t iDestIndex, int64_t iSrcIndex)
{
	pVecPositions[iDestIndex] = pVecPositions[iSrcIndex];
	pVecDirections[iDestIndex] = pVecDirections[iSrcIndex];
	puiAreaLights[iDestIndex] = puiAreaLights[iSrcIndex];
	puiPushers[iDestIndex] = puiPushers[iSrcIndex];
	puiTrails[iDestIndex] = puiTrails[iSrcIndex];
	puiSelfTargets[iDestIndex] = puiSelfTargets[iSrcIndex];
	pfDestroyedTimes[iDestIndex] = pfDestroyedTimes[iSrcIndex];

	pFlags[iDestIndex] = pFlags[iSrcIndex];
	pVecVelocities[iDestIndex] = pVecVelocities[iSrcIndex];
	pVecExplosionDirections[iDestIndex] = pVecExplosionDirections[iSrcIndex];
	puiTargets[iDestIndex] = puiTargets[iSrcIndex];
	pfExplosionRadii[iDestIndex] = pfExplosionRadii[iSrcIndex];
	pfTimes[iDestIndex] = pfTimes[iSrcIndex];
	pfDeltaRotations[iDestIndex] = pfDeltaRotations[iSrcIndex];
	pfDeltaRotationDelays[iDestIndex] = pfDeltaRotationDelays[iSrcIndex];
	pfExaustDelays[iDestIndex] = pfExaustDelays[iSrcIndex];
	pfNextJitter[iDestIndex] = pfNextJitter[iSrcIndex];
	pfDeltaRotationMax[iDestIndex] = pfDeltaRotationMax[iSrcIndex];
	pfExplosionTimes[iDestIndex] = pfExplosionTimes[iSrcIndex];
	pfAccelerations[iDestIndex] = pfAccelerations[iSrcIndex];
	pfPitches[iDestIndex] = pfPitches[iSrcIndex];
	puiSounds[iDestIndex] = puiSounds[iSrcIndex];
}

void Missiles::Global([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
}

void Missiles::Interpolate([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInputHeld& __restrict rFrameInputHeld, [[maybe_unused]] float fDeltaTime)
{
	Missiles& rCurrent = rFrame.missiles;
	const Missiles& rPrevious = rPreviousFrame.missiles;

	// 1. operator== 2. Copy() 3. Load/Save in Global() or Main() or PostRender() 4. Spawn()
	// Make sure to Remove() any pools in Destroy()
	VERIFY_SIZE(rCurrent, 36160);

	Spawnable::Interpolate(rCurrent, rPrevious);

	rCurrent.iCount = rPrevious.iCount;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		auto vecPosition = rPrevious.pVecPositions[i];
		auto vecDirection = rPrevious.pVecDirections[i];
		engine::area_light_t uiAreaLight = rPrevious.puiAreaLights[i];
		engine::pusher_t uiPusher = rPrevious.puiPushers[i];
		engine::trail_t uiTrail = rPrevious.puiTrails[i];
		engine::target_t uiSelfTarget = rPrevious.puiSelfTargets[i];
		float fDestroyedTime = std::max(0.0f, rPrevious.pfDestroyedTimes[i] - fDeltaTime);

		if (!(rPrevious.pFlags[i] & kExploding)) [[likely]]
		{
			vecPosition = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime), rPrevious.pVecVelocities[i], vecPosition);

			// Add delta rotation to direction
			float fDeltaRotationDelayPercent = std::clamp(1.0f - rPrevious.pfDeltaRotationDelays[i] / kfDeltaRotationDelay, 0.0f, 1.0f);
			vecDirection = XMVector3Normalize(XMVector4Transform(vecDirection, XMMatrixRotationZ(fDeltaTime * fDeltaRotationDelayPercent * rPrevious.pfDeltaRotations[i])));
		}

		// Update area light for exhaust flame
		if (rPrevious.pFlags[i] & kExploding) [[unlikely]]
		{
			rFrame.areaLights.Remove(uiAreaLight);
		}
		else if (rPrevious.pfExaustDelays[i] <= 0.0f)
		{
			float fLength = kfExhaustLength + common::Random<kfExhaustLengthRandom>(rFrame.randomEngine);
			float fWidth = kfExhaustWidth;
			if ((rFrame.iFrame) % 2 == 0)
			{
				fWidth = -fWidth;
			}

			auto vecExhaustOffset = XMVectorMultiply(XMVectorReplicate(kfExhaustOffset), XMVector3Normalize(vecDirection));
			auto vecExhaustDirection = XMVector3Normalize(XMVectorAdd(vecDirection, XMVector3Normalize(XMVectorSubtract(vecPosition, rPrevious.pVecPositions[i]))));
			const auto[vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible] = common::CalculateArea(XMVectorAdd(vecPosition, vecExhaustOffset), vecExhaustDirection, 0.0f, fLength, fWidth);
			const auto [vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting] = common::CalculateArea(XMVectorAdd(vecPosition, vecExhaustOffset), vecExhaustDirection, 0.0f, kfExhaustLightingArea * fLength, kfExhaustLightingArea * fWidth);
			rFrame.areaLights.Add(uiAreaLight,
			{
				.crc = rPrevious.pFlags[i] & kTargetPlayer ? data::kTexturesMissilesBC71pngCrc : data::kTexturesMissilesBC72pngCrc,
				.pf2Texcoords = { {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, },
				.pVecVisiblePositions = {vecTopLeftVisible, vecTopRightVisible, vecBottomLeftVisible, vecBottomRightVisible},
				.fVisibleIntensity = kfExhaustVisibleIntensity,
				.pVecLightingPositions = {vecTopLeftLighting, vecTopRightLighting, vecBottomLeftLighting, vecBottomRightLighting},
				.fLightingIntensity = kfExhaustLightingIntensity,
			});
		}

		// Update trail position
		float fTrailOffset = kfTrailOffset + kfTrailOffsetExtra * std::abs(rPrevious.pfDeltaRotations[i]);
		auto vecTrailOffset = XMVectorMultiply(XMVectorReplicate(fTrailOffset), XMVector3Normalize(vecDirection));

		rFrame.trails.Add(uiTrail, rFrame.fCurrentTime,
		{
			.vecPosition = vecPosition + (rPrevious.pFlags[i] & kExploding ? XMVectorZero() : vecTrailOffset),
			.fIntensity = kfTrailIntensity,
		});

		// Update self-target position
		if (rPrevious.pFlags[i] & kTargetPlayer && !(rPrevious.pFlags[i] & kExploding))
		{
			rFrame.targets.Add(uiSelfTarget,
			{
				.flags = {kDestination, kTargetIsEnemy},
				.vecPosition = vecPosition,
			});
		}
		else
		{
			rFrame.targets.Remove(rFrame, uiSelfTarget, {kDestination});
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;
		rCurrent.puiAreaLights[i] = uiAreaLight;
		rCurrent.puiPushers[i] = uiPusher;
		rCurrent.puiTrails[i] = uiTrail;
		rCurrent.puiSelfTargets[i] = uiSelfTarget;
		rCurrent.pfDestroyedTimes[i] = fDestroyedTime;
	}
}

void XM_CALLCONV SpawnMissileExplosion(Frame& __restrict rFrame, float fPercent, FXMVECTOR vecPosition, FXMVECTOR vecDirection, MissileFlags_t flags)
{
	ASSERT(fPercent > 0.0f);

	engine::explosion_t uiExplosion = 0;
	rFrame.explosions.Add(uiExplosion, rFrame,
	{
		.flags = {kDestroysSelf, kYellow},
		.vecPosition = vecPosition,
		.vecDirection = vecDirection,
		.uiParticleCount = static_cast<uint32_t>(fPercent * (flags & kDirectional ? 0.6f : 1.0f) * kfExplosionParticleCount),
		.fParticleAngle = flags & kDirectional ? XM_PI : XM_2PI,
		.uiTrailCount = static_cast<uint32_t>(fPercent * (kfExplosionTrailCountMin + common::Random<kfExplosionTrailCountRandom>(rFrame.randomEngine))),
		.fTrailAngle = XM_PI,
		.fLightPercent = 1.0f,
		.fPusherPercent = 0.0f,
		.fSizePercent = 0.25f + fPercent,
		.fSmokePercent = flags & kDirectional ? fPercent : 0.5f * fPercent,
		.fTimePercent = fPercent,
	});
}

void Missiles::PostRender([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Missiles& rCurrent = rFrame.missiles;
	const Missiles& rPrevious = rPreviousFrame.missiles;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		MissileFlags_t flags = rPrevious.pFlags[i];
		auto vecVelocity = rPrevious.pVecVelocities[i];
		auto vecExplosionDirections = rPrevious.pVecExplosionDirections[i];
		engine::target_t uiTarget = rPrevious.puiTargets[i];
		float fExplosionRadius = rPrevious.pfExplosionRadii[i];
		float fTimes = rPrevious.pfTimes[i] + fDeltaTime;
		float fDeltaRotation = rPrevious.pfDeltaRotations[i];
		float fDeltaRotationDelay = rPrevious.pfDeltaRotationDelays[i];
		float fExaustDelay = rPrevious.pfExaustDelays[i] - fDeltaTime;
		float fNextJitter = rPrevious.pfNextJitter[i] - fDeltaTime;
		float fDeltaRotationMax = rPrevious.pfDeltaRotationMax[i];
		float fExplosionTime = rPrevious.pfExplosionTimes[i];
		float fAcceleration = rPrevious.pfAccelerations[i];
		float fPitch = rPrevious.pfPitches[i];
		engine::sound_t uiSound = rPrevious.puiSounds[i];

		// Decay velocity
		vecVelocity = XMVectorMultiply(XMVectorReplicate(1.0f - fDeltaTime * kfVelocityDecay), vecVelocity);

		// Accelerate
		float fDeltaAnglePercent = std::abs(fDeltaRotation) / fDeltaRotationMax;
		ASSERT(fDeltaAnglePercent >= 0.0f && fDeltaAnglePercent <= 1.0f);

		float fAdjustedAcceleration = (1.0f - fDeltaAnglePercent) * fAcceleration + fDeltaAnglePercent * kfAccelerationAtMaxDeltaAngle * fAcceleration;
		vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAdjustedAcceleration), rCurrent.pVecDirections[i], vecVelocity);

		// Rotate velocity towards direction
		float fVelocityToDirectionPercent = 1.0f - fDeltaTime * kfVelocityToDirection;
		auto vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fVelocityToDirectionPercent), XMVector3Normalize(vecVelocity));
		auto vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fVelocityToDirectionPercent), rCurrent.pVecDirections[i]);
		vecVelocity = XMVectorMultiply(XMVector3Length(vecVelocity), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));

		// Decay delta rotation
		fDeltaRotation = (1.0f - fDeltaTime * kfDeltaRotationDecay) * fDeltaRotation;

		// Jitter direction
		if (fNextJitter < 0.0f)
		{
			fNextJitter = common::Random<kfJitterIntervalRandom>(rFrame.randomEngine);

			uint32_t uiRandom = common::Random(2, rFrame.randomEngine);
			float fDeltaAnglePercentExtra = 1.0f + 3.0f * fDeltaAnglePercent;
			float fDeltaAngleJitter = uiTarget == 0 ? kfDeltaAngleJitterRandom : kfDeltaAngleJitterRandomWithTarget;
			if (uiRandom == 0) { vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fDeltaAnglePercentExtra * (-kfDirectionJitterRandom + common::Random<2.0f * kfDirectionJitterRandom>(rFrame.randomEngine)))); };
			if (uiRandom == 1) { fDeltaRotation += fDeltaAnglePercentExtra * (-fDeltaAngleJitter + fDeltaAngleJitter * common::Random<2.0f>(rFrame.randomEngine)); }
			if (uiRandom == 2) { rCurrent.pVecPositions[i] += XMVectorSet(-kfPositionJitterRandom + common::Random<2.0f * kfPositionJitterRandom>(rFrame.randomEngine), -kfPositionJitterRandom + common::Random<2.0f * kfPositionJitterRandom>(rFrame.randomEngine), 0.0f, 0.0f); }
		}

		// Increase delta rotation towards target
		if (uiTarget != 0)
		{
			fDeltaRotationDelay -= fDeltaTime;

			engine::TargetInfo& rTargetInfo = rFrame.targets.GetInfo(uiTarget);
			if (rTargetInfo.flags & engine::TargetFlags::kDestination)
			{
				auto vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(rTargetInfo.vecPosition, rCurrent.pVecPositions[i]));
				float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(rCurrent.pVecDirections[i], vecToDestinationNormal));
				float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaRotationTowardsTarget : -kfDeltaRotationTowardsTarget;

				fDeltaRotation = kfDeltaRotationChange * fDeltaRotation + (1.0f - kfDeltaRotationChange) * fWantedDeltaRotation;
			}
			else
			{
				rFrame.targets.Remove(rFrame, uiTarget, engine::TargetFlags::kSubscriber);
			}
		}

		// Clamp delta rotation
		fDeltaRotation = common::MinAbs(fDeltaRotation, fDeltaRotationMax);

		// Spawn destruction explosions
		fExplosionTime = fExplosionTime - fDeltaTime;
		if (flags & kExploding && fExplosionTime < 0.0f)
		{
			fExplosionTime = kfDestroyExplosionInterval;

			float fPercent = std::max(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.1f);
			auto vecPosition = XMVectorAdd(fExplosionRadius * XMVectorSet(-kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.randomEngine), -kfExplosionPositionJitter + common::Random<2.0f * kfExplosionPositionJitter>(rFrame.randomEngine), 0.0f, 0.0f), rCurrent.pVecPositions[i]);
			SpawnMissileExplosion(rFrame, fPercent, vecPosition, vecExplosionDirections, flags);
		}

		// Apply pushers (at reduced intensity)
		if (!(rCurrent.pFlags[i] & kExploding)) [[likely]]
		{
			vecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(0.1f * fDeltaTime), rFrame.pushers.ApplyPush(rCurrent.pVecPositions[i]), vecVelocity);
		}

		// Sound position
		if (flags & kExploding) [[unlikely]]
		{
			rFrame.sounds.Remove(uiSound);
		}
		else
		{
			rFrame.sounds.Add(uiSound,
			{
				.uiCrc = data::kAudioMissile182794__qubodup__rocketlaunchwavCrc,
				.fVolume = 0.175f,
				.fPitch = rPrevious.pfPitches[i],
				.fFadeOutTime = 0.04f,
				.vecPosition = rCurrent.pVecPositions[i],
				.vecVelocity = rPrevious.pVecVelocities[i],
			});
		}

		// Some targets like turrets might not be at base height
		vecVelocity = XMVectorSetZ(vecVelocity, 0.0f);


		// Save
		rCurrent.pFlags[i] = flags;
		rCurrent.pVecVelocities[i] = vecVelocity;
		rCurrent.pVecExplosionDirections[i] = vecExplosionDirections;
		rCurrent.puiTargets[i] = uiTarget;
		rCurrent.pfExplosionRadii[i] = fExplosionRadius;
		rCurrent.pfTimes[i] = fTimes;
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

void Missiles::Explode(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, int64_t i, bool bDirectional)
{
	Missiles& rCurrent = rFrame.missiles;

	if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
	{
		DEBUG_BREAK();
		return;
	}

	Frame::AreaDamage(rFrame, rFrameInput, rCurrent.pVecPositions[i], kfMissileDamage, kfMissileDamageRadius);

	engine::gpAudioManager->PlayOneShot(data::kAudioExplosions80401__steveygos93__explosion2wavCrc, rCurrent.pVecPositions[i], 0.7f);

	rCurrent.pFlags[i] |= kExploding;
	if (bDirectional)
	{
		rCurrent.pFlags[i] |= kDirectional;
	}
	rCurrent.pVecExplosionDirections[i] = bDirectional ? engine::gpIslands->GlobalNormal(rCurrent.pVecPositions[i]) : XMVectorSet(1.0f, 0.0, 0.0f, 0.0f);
	rCurrent.pfDestroyedTimes[i] = kfDestroyTime;
	rCurrent.pfExplosionTimes[i] = kfDestroyExplosionInterval;

	rFrame.targets.Remove(rFrame, rCurrent.puiTargets[i], engine::TargetFlags::kSubscriber);

	SpawnMissileExplosion(rFrame, 1.0f, rCurrent.pVecPositions[i], rCurrent.pVecExplosionDirections[i], rCurrent.pFlags[i]);
}

void Missiles::Collide([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Missiles& rCurrent = rFrame.missiles;

	// Collide terrain
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		float fElevationFinal = engine::gpIslands->GlobalElevation(rCurrent.pVecPositions[i]);

		// Check to see if the final position has entered into the terrain
		if (XMVectorGetZ(rCurrent.pVecPositions[i]) > fElevationFinal)
		{
			continue;
		}
		
		// Start destroying this missile
		Explode(rFrame, rFrameInput, i, true);
	}

	// Collide enemy missiles with player blasters
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		if (rCurrent.pFlags[i] & kTargetEnemy)
		{
			continue;
		}

		for (int64_t j = 0; j < rFrame.blasters.iCount; ++j)
		{
			if (rFrame.blasters.pFlags[j] & BlasterFlags::kImpactObject || !(rFrame.blasters.pFlags[j] & BlasterFlags::kCollideEnemies))
			{
				continue;
			}

			float fDistance = common::Distance(rFrame.blasters.pVecPositions[j], rCurrent.pVecPositions[i]);
			if (fDistance > kfMissileCollisionRadius) [[likely]]
			{
				continue;
			}

			Blasters::CollisionEffect(rFrame, j);
			Frame::BlasterImpact(rFrame, j, rCurrent.pVecPositions[i]);

			Explode(rFrame, rFrameInput, i, false);
			break;
		}
	}

	// Take player area damage
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		if (rCurrent.pFlags[i] & kTargetEnemy)
		{
			continue;
		}

		for (decltype(rFrame.playerAreas.uiMaxIndex) j = 0; j <= rFrame.playerAreas.uiMaxIndex; ++j)
		{
			if (!rFrame.playerAreas.pbUsed[j])
			{
				continue;
			}

			engine::AreaInfo& rAreaInfo = rFrame.playerAreas.pObjectInfos[j];
			if (common::InsideAreaVertices(rCurrent.pVecPositions[i], rAreaInfo.areaVertices)) [[unlikely]]
			{
				Explode(rFrame, rFrameInput, i, false);
			}
		}
	}

	// Collide player missiles with enemy missiles
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		if (rCurrent.pFlags[i] & kTargetPlayer)
		{
			continue;
		}

		for (int64_t j = 0; j < rCurrent.iCount; ++j)
		{
			if (rCurrent.pFlags[j] & kTargetEnemy)
			{
				continue;
			}

			float fDistance = common::Distance(rCurrent.pVecPositions[j], rCurrent.pVecPositions[i]);
			if (fDistance > kfToMissileCollisionRadius) [[likely]]
			{
				continue;
			}

			Explode(rFrame, rFrameInput, i, false);
			break;
		}
	}
}

void Missiles::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Missiles& rCurrent = rFrame.missiles;

	for (int64_t j = 0; j < rCurrent.iSpawnCount; ++j)
	{
		if (rCurrent.iCount == kiMax)
		{
			DEBUG_BREAK();
			break;
		}

		int64_t i = rCurrent.iCount++;

		rCurrent.pVecPositions[i] = rCurrent.pSpawns[j].vecPosition;
		rCurrent.pVecDirections[i] = rCurrent.pSpawns[j].vecDirection;
		rCurrent.puiAreaLights[i] = 0;
		rCurrent.puiPushers[i] = 0;
		rCurrent.puiTrails[i] = 0;
		rCurrent.puiSelfTargets[i] = 0;

		rCurrent.pFlags[i] = rCurrent.pSpawns[j].flags;
		rCurrent.pVecVelocities[i] = rCurrent.pSpawns[j].vecVelocity;
		rCurrent.pVecExplosionDirections[i] = XMVectorSet(1.0f, 0.0, 0.0f, 0.0f);
		rCurrent.puiTargets[i] = rCurrent.pSpawns[j].uiTarget;
		rCurrent.pfExplosionRadii[i] = 0.0f;
		rCurrent.pfTimes[i] = 0.0f;
		rCurrent.pfDeltaRotationDelays[i] = 0.5f * kfDeltaRotationDelay + common::Random<kfDeltaRotationDelay>(rFrame.randomEngine);
		rCurrent.pfDeltaRotations[i] = 0.0f;
		rCurrent.pfExaustDelays[i] = kfExhaustDelay;
		rCurrent.pfNextJitter[i] = 0.0f;
		rCurrent.pfDeltaRotationMax[i] = kfDeltaRotationLimitMin + common::Random<kfDeltaRotationLimitRandom>(rFrame.randomEngine);
		rCurrent.pfDestroyedTimes[i] = 0;
		rCurrent.pfExplosionTimes[i] = 0;
		rCurrent.pfAccelerations[i] = rCurrent.pSpawns[j].fAcceleration;
		static constexpr float kfPitchMin = 0.75f;
		static constexpr float kfPitchRandom = 0.5f;
		rCurrent.pfPitches[i] = kfPitchMin + common::Random<kfPitchRandom>(rFrame.randomEngine);
		rCurrent.puiSounds[i] = 0;
	}

	rCurrent.iSpawnCount = 0;
}

void Missiles::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] int64_t i)
{
	Missiles& rCurrent = rFrame.missiles;

	rFrame.areaLights.Remove(rCurrent.puiAreaLights[i]);
	rFrame.pushers.Remove(rCurrent.puiPushers[i]);
	rFrame.targets.Remove(rFrame, rCurrent.puiTargets[i], {engine::TargetFlags::kSubscriber});
	rFrame.trails.Remove(rCurrent.puiTrails[i]);
	rFrame.targets.Remove(rFrame, rCurrent.puiSelfTargets[i], {engine::TargetFlags::kDestination});
	rFrame.sounds.Remove(rCurrent.puiSounds[i]);
}

void Missiles::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameInput& __restrict rFrameInput, [[maybe_unused]] float fDeltaTime)
{
	Missiles& rCurrent = rFrame.missiles;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		bool bDestroy = engine::OutsideVisibleArea(rFrameInput, rCurrent.pVecPositions[i], kfAutoDestroyDistance, kfAutoDestroyDistance, kfAutoDestroyDistance, kfAutoDestroyDistance);
		bDestroy |= rCurrent.pFlags[i] & kDestroy;
		bDestroy |= rCurrent.pFlags[i] & kExploding && rCurrent.pfDestroyedTimes[i] <= 0.0f;
		if (!bDestroy) [[likely]]
		{
			continue;
		}

		Destroy(rFrame, i);

		if (rCurrent.iCount - 1 > i) [[likely]]
		{
			rCurrent.Copy(i, rCurrent.iCount - 1);
			--i;
		}

		--rCurrent.iCount;
	}
}

void Missiles::RenderGlobal([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
}

void Missiles::RenderMain([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const Frame& __restrict rFrame)
{
	const Missiles& rCurrent = rFrame.missiles;

	static auto sMatPreMovePlayer = XMMatrixTranslation(0.0f, 0.0f, 0.0f);
	static auto sMatPreRotatePlayer = XMMatrixRotationX(XM_PIDIV2) * XMMatrixRotationZ(XM_PIDIV2);
	static constexpr float kfScalePlayer = 0.5f;
	static constexpr float kfWidthPlayer = 2.0f;

	PROFILE_SET_COUNT(engine::kCpuCounterMissiles, rCurrent.iCount);
	auto pPlayerLayouts = reinterpret_cast<shaders::GltfLayout*>(engine::gpBufferManager->mPlayerMissilesStorageBuffers.at(iCommandBuffer).mpMappedMemory);

	int64_t iPlayerMissilesRendered = 0;
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		XMFLOAT4A f4Position {};
		XMStoreFloat4A(&f4Position, rCurrent.pVecPositions[i]);
		if (!engine::InVisibleArea(engine::gf4RenderVisibleArea, f4Position))
		{
			continue;
		}

		float fScale = kfScalePlayer;
		if (rCurrent.pFlags[i] & kExploding) [[unlikely]]
		{
			fScale *= std::pow(rCurrent.pfDestroyedTimes[i] / kfDestroyTime, 0.5f);
		}

		auto matScaling = XMMatrixScaling(fScale, fScale, kfWidthPlayer * fScale);
		auto matYaw = common::RotationMatrixFromDirection(rCurrent.pVecDirections[i], XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
		auto matTranslation = XMMatrixTranslationFromVector(rCurrent.pVecPositions[i]);

		auto matTransform = sMatPreMovePlayer * matScaling * sMatPreRotatePlayer * matYaw * matTranslation;

		shaders::GltfLayout& rGltfLayout = pPlayerLayouts[iPlayerMissilesRendered++];
		rGltfLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rGltfLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));
		rGltfLayout.f4ColorAdd = {};
	}
	PROFILE_SET_COUNT(engine::kCpuCounterMissilesRendered, iPlayerMissilesRendered);

	gpGltfPipelines->mpGltfPipelines[kGltfPipelinePlayerMissiles].WriteIndirectBuffer(iCommandBuffer, iPlayerMissilesRendered);
	gpGltfPipelines->mpGltfPipelines[kGltfPipelinePlayerMissilesShadow].WriteIndirectBuffer(iCommandBuffer, iPlayerMissilesRendered);
}

} // namespace game
