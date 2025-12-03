#include "Explosions.h"

#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Frame/Collections/Trails.h"
#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Graphics/Managers/ParticleManager.h"

namespace engine
{

using enum ExplosionFlags;

// Static indices for registered explosion effect types
static uint8_t suiExplosionPointLightTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionPuffTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionTrailTypeIndex = kuiInvalidTrailType;

void ExplosionsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
	ExplosionsInterpolate& rCurrent = rCurrentFrameInterpolate.explosions;
	const ExplosionsInterpolate& rPrevious = rPreviousFrame.interpolate.explosions;
	TrailsInterpolate& rTrails = rCurrentFrameInterpolate.trails;

	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + fDeltaTime;

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rPrevious.puiTypeIndices[i];
		ExplosionFlags_t flags = rPrevious.pFlags[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];

		float fLightPercent = rPrevious.pfLightPercents[i];
		float fPusherPercent = rPrevious.pfPusherPercents[i];
		float fSizePercent = rPrevious.pfSizePercents[i];
		float fSmokePercent = rPrevious.pfSmokePercents[i];
		float fTimePercent = rPrevious.pfTimePercents[i];

		int32_t iTrailCount = rPrevious.piTrailCounts[i];
		pusher_t pusher = rPrevious.pPushers[i];

		// Save
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pFlags[i] = flags;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;

		rCurrent.pfLightPercents[i] = fLightPercent;
		rCurrent.pfPusherPercents[i] = fPusherPercent;
		rCurrent.pfSizePercents[i] = fSizePercent;
		rCurrent.pfSmokePercents[i] = fSmokePercent;
		rCurrent.pfTimePercents[i] = fTimePercent;

		rCurrent.piTrailCounts[i] = iTrailCount;
		rCurrent.pPushers[i] = pusher;

		// Copy trail arrays
		for (int64_t j = 0; j < iTrailCount; ++j)
		{
			rCurrent.pTrails[j][i] = rPrevious.pTrails[j][i];
			rCurrent.pfTrailTimes[j][i] = rPrevious.pfTrailTimes[j][i];
			rCurrent.pfTrailIntensities[j][i] = rPrevious.pfTrailIntensities[j][i];
			rCurrent.pVecTrailStartPositions[j][i] = rPrevious.pVecTrailStartPositions[j][i];
			rCurrent.pVecTrailEndPositions[j][i] = rPrevious.pVecTrailEndPositions[j][i];
		}

		// Update trail positions with gravity (merged from Sync)
		const ExplosionType& rType = sTypes.at(uiTypeIndex);
		float fExplosionTime = fCurrentTime - fStartTime;

		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			trails_t trailId = rCurrent.pTrails[j][i];
			if (!trailId.IsValid())
			{
				continue;
			}

			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rCurrent.pfTrailTimes[j][i];

			// Skip expired trails (removal happens in Destroy phase)
			if (fExplosionTime >= fTrailEndTime)
			{
				continue;
			}

			// Calculate trail position with gravity
			float fTrailPercent = (fExplosionTime - fTimePercent * rType.fTrailDelayTime) / rCurrent.pfTrailTimes[j][i];
			fTrailPercent = std::clamp(fTrailPercent, 0.0f, 1.0f);

			XMVECTOR vecTrailStart = rCurrent.pVecTrailStartPositions[j][i];
			XMVECTOR vecTrailEnd = rCurrent.pVecTrailEndPositions[j][i];
			XMVECTOR vecGravityOffset = XMVectorSet(0.0f, 0.0f, fTrailPercent * rType.fTrailGravity, 0.0f);

			XMVECTOR vecTrailPosition = XMVectorLerp(vecTrailStart, vecTrailEnd - vecGravityOffset, fTrailPercent);
			float fTrailIntensity = (1.0f - fTrailPercent) * rCurrent.pfTrailIntensities[j][i];

			// Update trail in Trails collection
			int64_t iTrailIndex = rTrails.IdToIndex(trailId);
			rTrails.pVecPositions[iTrailIndex] = vecTrailPosition;
			rTrails.pfIntensities[iTrailIndex] = fTrailIntensity;
		}
	}
}

void ExplosionsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	engine::ReallocateAndCopyMetadata(rPostRender, rPreviousFrame.postRender.explosions, rPostRender.Members());

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	// Manage pusher lifecycle for all explosions
	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiTypeIndex = rInterpolate.puiTypeIndices[i];
		const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(uiTypeIndex);

		float fStartTime = rInterpolate.pfStartTimes[i];
		float fExplosionTime = fCurrentTime - fStartTime;
		float fTimePercent = rInterpolate.pfTimePercents[i];
		float fPusherPercent = rInterpolate.pfPusherPercents[i];
		XMVECTOR vecPosition = rInterpolate.pVecPositions[i];

		float fScaledPusherStart = fTimePercent * rType.fPusherStartTime;
		float fScaledPusherEnd = fTimePercent * rType.fPusherEndTime;

		if (fExplosionTime >= fScaledPusherStart && fExplosionTime <= fScaledPusherEnd)
		{
			float fPusherDuration = fScaledPusherEnd - fScaledPusherStart;
			float fPusherProgress = (fExplosionTime - fScaledPusherStart) / fPusherDuration;

			float fRadius = rType.fPusherRadius + 3.0f * fPusherProgress * rType.fPusherRadius;
			float fIntensity = fPusherPercent * std::pow(1.0f - fPusherProgress, 3.0f) * rType.fPusherIntensity;

			pusher_t pusher = rInterpolate.pPushers[i];
			if (!pusher.IsValid())
			{
				// Create pusher
				rInterpolate.pPushers[i] = PushersPostRender::Add(rFrame, vecPosition, fRadius, fIntensity, rType.fPusherPower, PusherFlags::kTypeDefault);
			}
			else
			{
				// Update pusher
				PushersPostRender::UpdateRadius(rFrame, pusher, fRadius);
				PushersPostRender::UpdateIntensity(rFrame, pusher, fIntensity);
			}
		}
		else if (rInterpolate.pPushers[i].IsValid())
		{
			// Remove pusher (past end time)
			PushersPostRender::Remove(rFrame, rInterpolate.pPushers[i]);
			rInterpolate.pPushers[i] = pusher_t {};
		}
	}
}

uint8_t ExplosionsPostRender::RegisterType(const ExplosionType& rType)
{
	ExplosionsInterpolate::sTypes.push_back(rType);
	return static_cast<uint8_t>(ExplosionsInterpolate::sTypes.size() - 1);
}

const ExplosionType& ExplosionsPostRender::GetType(uint8_t uiIndex)
{
	return ExplosionsInterpolate::sTypes.at(uiIndex);
}

void ExplosionsPostRender::Register()
{
	// Guard against double registration
	if (suiExplosionPointLightTypeIndex != kuiInvalidControllerType)
	{
		return;
	}

	// Constants from old Pools/Explosions.cpp
	static constexpr float kfPrimaryTime = 0.075f;

	// Light constants
	static constexpr float kfPrimaryVisibleSize = 1.25f;
	static constexpr float kfPrimaryVisibleIntensity = 0.6f;
	static constexpr float kfPrimaryLightingSize = 2.25f;
	static constexpr float kfPrimaryLightingIntensity = 900.0f;
	static constexpr float kfSecondaryVisibleSize = 1.25f;
	static constexpr float kfSecondaryVisibleIntensity = kfPrimaryVisibleIntensity;
	static constexpr float kfSecondaryLightingSize = 0.75f * kfPrimaryLightingSize;
	static constexpr float kfSecondaryLightingIntensity = 0.25f * kfPrimaryLightingIntensity;

	// Puff constants
	static constexpr float kfPrimaryPuffSize = 2.0f;
	static constexpr float kfPrimaryPuffStartTime = 0.0f;
	static constexpr float kfPrimaryPuffEndTime = 0.2f;
	static constexpr float kfPrimaryPuffIntensity = 4.0f / (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
	static constexpr float kfSecondaryPuffTimes = 0.5f * (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
	static constexpr float kfSecondaryPuffIntensity = 1.0f / kfSecondaryPuffTimes;

	// Register PointLights::Type for explosions
	suiExplosionPointLightTypeIndex = PointLightsPostRender::RegisterType(
	{
		.crc = data::kTexturesBC7ExplosionpngCrc,
		.uiColor = 0xFFFFFFFF,
		.fVisibleArea = kfPrimaryVisibleSize,
		.fVisibleIntensity = kfPrimaryVisibleIntensity,
		.fLightingArea = kfPrimaryLightingSize,
		.fLightingIntensity = kfPrimaryLightingIntensity,
	});

	// Register primary light controller type (3-keyframe: start -> peak -> fade)
	suiPrimaryLightControllerTypeIndex = PointLightsInterpolate::RegisterControllerType(
	{
		.uiBaseTypeIndex = suiExplosionPointLightTypeIndex,
		.uiKeyframeCount = 3,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, 0.4f * kfPrimaryTime, 3.0f * kfPrimaryTime, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 0.3f * kfPrimaryVisibleSize, .fVisibleIntensity = 0.25f * kfPrimaryVisibleIntensity, .fLightingArea = 0.6f * kfPrimaryLightingSize, .fLightingIntensity = 0.25f * kfPrimaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.6f * kfPrimaryVisibleSize, .fVisibleIntensity = kfPrimaryVisibleIntensity, .fLightingArea = 1.5f * kfPrimaryLightingSize, .fLightingIntensity = kfPrimaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.6f * kfPrimaryVisibleSize, .fVisibleIntensity = 0.0f, .fLightingArea = 1.2f * kfPrimaryLightingSize, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{},
		},
	});

	// Register secondary light controller type (3-keyframe: delayed start -> peak -> fade)
	suiSecondaryLightControllerTypeIndex = PointLightsInterpolate::RegisterControllerType(
	{
		.uiBaseTypeIndex = suiExplosionPointLightTypeIndex,
		.uiKeyframeCount = 3,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, 1.0f * kfPrimaryTime, 3.0f * kfPrimaryTime, 0.0f},
		.keyframes =
		{
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{.fVisibleArea = kfSecondaryVisibleSize, .fVisibleIntensity = kfSecondaryVisibleIntensity, .fLightingArea = 2.0f * kfSecondaryLightingSize, .fLightingIntensity = kfSecondaryLightingIntensity, .fRotation = 0.0f},
			{.fVisibleArea = 0.0f, .fVisibleIntensity = 0.0f, .fLightingArea = 0.0f, .fLightingIntensity = 0.0f, .fRotation = 0.0f},
			{},
		},
	});

	// Register Puffs::Type for explosions
	suiExplosionPuffTypeIndex = PuffsPostRender::RegisterType(
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
	});

	// Register primary puff controller type (2-keyframe: start -> expand)
	suiPrimaryPuffControllerTypeIndex = PuffsInterpolate::RegisterControllerType(
	{
		.uiBaseTypeIndex = suiExplosionPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {kfPrimaryPuffStartTime, kfPrimaryPuffEndTime, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 0.1f * kfPrimaryPuffSize, .fIntensity = kfPrimaryPuffIntensity, .fRotation = 0.0f},
			{.fArea = 0.6f * kfPrimaryPuffSize, .fIntensity = kfPrimaryPuffIntensity, .fRotation = 0.0f},
			{},
			{},
		},
	});

	// Register secondary puff controller type (2-keyframe: smaller, shorter)
	suiSecondaryPuffControllerTypeIndex = PuffsInterpolate::RegisterControllerType(
	{
		.uiBaseTypeIndex = suiExplosionPuffTypeIndex,
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfSecondaryPuffTimes, 0.0f, 0.0f},
		.keyframes =
		{
			{.fArea = 0.1f * kfPrimaryPuffSize, .fIntensity = kfSecondaryPuffIntensity, .fRotation = 0.0f},
			{.fArea = 0.4f * kfPrimaryPuffSize, .fIntensity = kfSecondaryPuffIntensity, .fRotation = 0.0f},
			{},
			{},
		},
	});

	// Register Trails::Type for explosion trails
	suiExplosionTrailTypeIndex = TrailsPostRender::RegisterType(
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
	});
}

uint8_t ExplosionsPostRender::GetPrimaryLightControllerTypeIndex()
{
	return suiPrimaryLightControllerTypeIndex;
}

uint8_t ExplosionsPostRender::GetSecondaryLightControllerTypeIndex()
{
	return suiSecondaryLightControllerTypeIndex;
}

uint8_t ExplosionsPostRender::GetPrimaryPuffControllerTypeIndex()
{
	return suiPrimaryPuffControllerTypeIndex;
}

uint8_t ExplosionsPostRender::GetSecondaryPuffControllerTypeIndex()
{
	return suiSecondaryPuffControllerTypeIndex;
}

uint8_t ExplosionsPostRender::GetTrailTypeIndex()
{
	return suiExplosionTrailTypeIndex;
}

ExplosionType ExplosionsPostRender::CreateDefaultType()
{
	return ExplosionType
	{
		.uiPrimaryLightControllerTypeIndex = suiPrimaryLightControllerTypeIndex,
		.uiSecondaryLightControllerTypeIndex = suiSecondaryLightControllerTypeIndex,
		.uiPrimaryPuffControllerTypeIndex = suiPrimaryPuffControllerTypeIndex,
		.uiSecondaryPuffControllerTypeIndex = suiSecondaryPuffControllerTypeIndex,
		.uiTrailTypeIndex = suiExplosionTrailTypeIndex,
	};
}

void XM_CALLCONV ExplosionsPostRender::Spawn(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiTypeIndex,
                                              FXMVECTOR vecPosition, FXMVECTOR vecDirection, ExplosionFlags_t flags,
                                              uint32_t uiTrailCount, float fTrailAngle,
                                              uint32_t uiParticleCount, float fParticleAngle,
                                              float fLightPercent, float fPusherPercent,
                                              float fSizePercent, float fSmokePercent, float fTimePercent)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(uiTypeIndex);

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = engine::AddElement(rInterpolate, rPostRender);

	// Initialize explosion data
	rInterpolate.puiTypeIndices[iSpawnIndex] = uiTypeIndex;
	rInterpolate.pFlags[iSpawnIndex] = flags;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
	rInterpolate.pVecPositions[iSpawnIndex] = vecPosition;
	rInterpolate.pVecDirections[iSpawnIndex] = vecDirection;

	rInterpolate.pfLightPercents[iSpawnIndex] = fLightPercent;
	rInterpolate.pfPusherPercents[iSpawnIndex] = fPusherPercent;
	rInterpolate.pfSizePercents[iSpawnIndex] = fSizePercent;
	rInterpolate.pfSmokePercents[iSpawnIndex] = fSmokePercent;
	rInterpolate.pfTimePercents[iSpawnIndex] = fTimePercent;

	rInterpolate.piTrailCounts[iSpawnIndex] = static_cast<int32_t>(std::min(uiTrailCount, static_cast<uint32_t>(kiMaxExplosionTrails)));
	rInterpolate.pPushers[iSpawnIndex] = pusher_t {};

	// Initialize trail arrays to invalid
	for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
	{
		rInterpolate.pTrails[j][iSpawnIndex] = trails_t {};
		rInterpolate.pfTrailTimes[j][iSpawnIndex] = 0.0f;
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = 0.0f;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = XMVectorZero();
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = XMVectorZero();
	}

	// Fire-and-forget effects: Primary light
	if (rType.uiPrimaryLightControllerTypeIndex != kuiInvalidControllerType)
	{
		float fPrimaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
		PointLightsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryLightControllerTypeIndex, vecPosition, fPrimaryRotation);
	}

	// Fire-and-forget effects: Primary puff
	if (rType.uiPrimaryPuffControllerTypeIndex != kuiInvalidControllerType)
	{
		PuffsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryPuffControllerTypeIndex, vecPosition);
	}

	// Fire-and-forget effects: Secondary explosions (4 staggered)
	static constexpr int64_t kiSecondaryExplosions = 4;
	float fDelayDelta = (0.75f * fTimePercent * rType.fPrimaryTime) / static_cast<float>(kiSecondaryExplosions);
	float fDelay = fDelayDelta;

	for (int64_t k = 0; k < kiSecondaryExplosions; ++k, fDelay += fDelayDelta)
	{
		// Calculate secondary explosion position
		XMVECTOR vecSecondaryOffset = XMVector3Rotate(
		    XMVectorSet(rType.fSecondaryPositionMin + std::pow(fSizePercent, 1.5f) * common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fSecondaryPositionJitter, 0.0f, 0.0f, 0.0f),
		    XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, common::Random<XM_2PI>(rFrame.postRender.randomEngine)));
		XMVECTOR vecSecondaryPosition = XMVectorAdd(vecSecondaryOffset, vecPosition);

		// Secondary light
		if (rType.uiSecondaryLightControllerTypeIndex != kuiInvalidControllerType)
		{
			float fSecondaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
			PointLightsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryLightControllerTypeIndex, vecSecondaryPosition, fSecondaryRotation);
		}

		// Secondary puff
		if (rType.uiSecondaryPuffControllerTypeIndex != kuiInvalidControllerType)
		{
			PuffsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryPuffControllerTypeIndex, vecSecondaryPosition);
		}
	}

	// Create trails
	XMVECTOR vecDirection2dNormal = XMVector3Normalize(XMVectorMultiply(XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f), vecDirection));
	int32_t iTrailCount = rInterpolate.piTrailCounts[iSpawnIndex];

	for (int32_t j = 0; j < iTrailCount; ++j)
	{
		float fTrailTime = fTimePercent * (rType.fTrailTimeMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailTimeRandom);
		float fTrailIntensity = fSmokePercent * (rType.fTrailIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailIntensityRandom);

		XMVECTOR vecTrailDirection = vecDirection2dNormal;
		if (j != 0)
		{
			vecTrailDirection = XMVector3Rotate(vecTrailDirection, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, fTrailAngle * (common::Random(rFrame.postRender.randomEngine) - 0.5f)));
		}

		XMVECTOR vecTrailStart = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(rType.fTrailStart), vecPosition);
		float fTrailLength = rType.fTrailLengthMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailLengthRandom;
		XMVECTOR vecTrailEnd = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(fTrailLength), vecPosition);

		// Create trail in Trails collection (start at full intensity, will fade over time in Sync)
		trails_t trailId = TrailsPostRender::Add(rFrame, fCurrentTime, rType.uiTrailTypeIndex, vecTrailStart, fTrailIntensity, 0.1f);

		rInterpolate.pTrails[j][iSpawnIndex] = trailId;
		rInterpolate.pfTrailTimes[j][iSpawnIndex] = fTrailTime;
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = fTrailIntensity;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = vecTrailStart;
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = vecTrailEnd;
	}

	// Spawn GPU particles
	uint32_t uiTotalParticles = uiParticleCount + rType.uiBaseParticleCount;
	for (uint32_t p = 0; p < uiTotalParticles; ++p)
	{
		XMFLOAT4A f4Position {};
		XMVECTOR vecParticlePosition = XMVectorAdd(vecPosition, XMVectorSet(
		    -rType.fParticlePositionJitter + common::Random<1.0f>(rFrame.postRender.randomEngine) * 2.0f * rType.fParticlePositionJitter,
		    -rType.fParticlePositionJitter + common::Random<1.0f>(rFrame.postRender.randomEngine) * 2.0f * rType.fParticlePositionJitter,
		    0.0f, 0.0f));
		XMStoreFloat4A(&f4Position, vecParticlePosition);

		float fVelocityMag = rType.fParticleVelocityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVelocityRandom;
		XMVECTOR vecVelocity = XMVectorMultiply(XMVectorReplicate(fVelocityMag), vecDirection2dNormal);
		vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, -0.5f * fParticleAngle + fParticleAngle * common::Random(rFrame.postRender.randomEngine)));
		vecVelocity = XMVectorSetZ(vecVelocity, common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVerticalVelocity);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecVelocity);

		// Calculate particle color based on flags
		uint32_t uiParticleColor = rType.uiParticleColor;
		if (flags & kYellow)
		{
			uiParticleColor |= ((100 + common::Random(25, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25, rFrame.postRender.randomEngine)) << 8);
		}
		else if (flags & kRed)
		{
			uiParticleColor |= ((50 + common::Random(25, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25, rFrame.postRender.randomEngine)) << 8);
		}

		float fParticleIntensity = rType.fParticleIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleIntensityRandom;

		ParticleManager::Spawn(gpParticleManager->mLongParticlesSpawnLayout,
		{
			.i4Misc = {static_cast<int32_t>(uiParticleColor), rType.iParticleCookie, static_cast<int32_t>(rType.fParticleLightingIntensity), 0},
			.f4MiscOne = {rType.fParticleVelocityDecay, rType.fParticleGravity, rType.fParticleIntensityDecay, rType.fParticleLightingSize},
			.f4MiscTwo = {rType.fParticleWidth, rType.fParticleLength, fParticleIntensity, rType.fParticleIntensityPower},
			.f4MiscThree = {},
			.f4Position = f4Position,
			.f4Velocity = f4Velocity,
		});
	}
}

void ExplosionsPostRender::Destroy(game::Frame& __restrict rFrame, float fCurrentTime)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiTypeIndex = rInterpolate.puiTypeIndices[i];
		const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(uiTypeIndex);

		float fStartTime = rInterpolate.pfStartTimes[i];
		float fExplosionTime = fCurrentTime - fStartTime;
		float fTimePercent = rInterpolate.pfTimePercents[i];

		// Remove expired trails (cleanup happens every frame, not just at explosion expiration)
		int32_t iTrailCount = rInterpolate.piTrailCounts[i];
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			trails_t trailId = rInterpolate.pTrails[j][i];
			if (!trailId.IsValid())
			{
				continue;
			}

			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			if (fExplosionTime >= fTrailEndTime)
			{
				TrailsPostRender::Remove(rFrame, trailId);
				rInterpolate.pTrails[j][i] = trails_t {};
			}
		}

		ExplosionFlags_t flags = rInterpolate.pFlags[i];

		// Skip non-self-destroying explosions for full removal
		if (!(flags & kDestroysSelf))
		{
			continue;
		}

		// Calculate end time (longest trail duration)
		float fEndTime = 0.0f;
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			fEndTime = std::max(fEndTime, fTrailEndTime);
		}

		// Also account for pusher end time
		fEndTime = std::max(fEndTime, fTimePercent * rType.fPusherEndTime);

		// Check if explosion has expired
		if (fExplosionTime < fEndTime)
		{
			continue;
		}

		// Cleanup: Remove pusher if still valid
		pusher_t pusher = rInterpolate.pPushers[i];
		if (pusher.IsValid())
		{
			PushersPostRender::Remove(rFrame, pusher);
		}

		// Remove the explosion using swap-and-pop
		if (rInterpolate.iCount - 1 > i)
		{
			engine::SwapElement(rInterpolate, i, rInterpolate.Members());
			engine::SwapElement(rPostRender, i, rPostRender.Members());
		}
		--rInterpolate.iCount;
		--rPostRender.iCount;
		--i; // Re-check this index (new element swapped in)
	}
}

bool ExplosionsInterpolate::operator==(const ExplosionsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecDirections[i], rOther.pVecDirections[i]);

		bEqual &= common::BreakOnNotEqual(pfLightPercents[i], rOther.pfLightPercents[i]);
		bEqual &= common::BreakOnNotEqual(pfPusherPercents[i], rOther.pfPusherPercents[i]);
		bEqual &= common::BreakOnNotEqual(pfSizePercents[i], rOther.pfSizePercents[i]);
		bEqual &= common::BreakOnNotEqual(pfSmokePercents[i], rOther.pfSmokePercents[i]);
		bEqual &= common::BreakOnNotEqual(pfTimePercents[i], rOther.pfTimePercents[i]);

		bEqual &= common::BreakOnNotEqual(piTrailCounts[i], rOther.piTrailCounts[i]);
		bEqual &= common::BreakOnNotEqual(pPushers[i], rOther.pPushers[i]);

		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pTrails[j][i], rOther.pTrails[j][i]);
			bEqual &= common::BreakOnNotEqual(pfTrailTimes[j][i], rOther.pfTrailTimes[j][i]);
			bEqual &= common::BreakOnNotEqual(pfTrailIntensities[j][i], rOther.pfTrailIntensities[j][i]);
			bEqual &= common::BreakOnNotEqual(pVecTrailStartPositions[j][i], rOther.pVecTrailStartPositions[j][i]);
			bEqual &= common::BreakOnNotEqual(pVecTrailEndPositions[j][i], rOther.pVecTrailEndPositions[j][i]);
		}
	}

	return bEqual;
}

bool ExplosionsPostRender::operator==(const ExplosionsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);
	return bEqual;
}

} // namespace engine
