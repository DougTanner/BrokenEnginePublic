#include "Explosions.h"

#ifdef BT_CLIENT
#include "Graphics/Graphics.h"
#include "Ui/WrapperBase.h"
#include "Frame/Collections/PointLights.h"
#include "Frame/Collections/Puffs.h"
#include "Frame/Collections/SmokeTrails.h"
#include "Frame/Collections/WindRadials.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Profile/ProfileManager.h"
#include "Data/Texture.h"
#endif

#include "Frame/Frame.h"

namespace engine
{

using enum ExplosionFlags;

#ifdef BT_CLIENT

// Wind
constexpr float kfWindDepositDuration = 0.3f;

// Explosion timing
constexpr float kfPrimaryTime = 0.06f;

// Primary light
constexpr float kfPrimaryVisibleSize = 1.0f;
constexpr float kfPrimaryVisibleIntensity = 0.6f;
constexpr float kfPrimaryLightingSize = 2.0f;
constexpr float kfPrimaryLightingIntensity = 450.0f;

// Secondary light
constexpr float kfSecondaryVisibleSize = 0.75f;
constexpr float kfSecondaryVisibleIntensity = kfPrimaryVisibleIntensity;
constexpr float kfSecondaryLightingSize = 0.75f * kfPrimaryLightingSize;
constexpr float kfSecondaryLightingIntensity = 0.75f * kfPrimaryLightingIntensity;

// Primary puff
constexpr float kfPrimaryPuffSize = 1.75f;
constexpr float kfPrimaryPuffStartTime = 0.0f;
constexpr float kfPrimaryPuffEndTime = 0.2f;
constexpr float kfPrimaryPuffIntensity = 0.5f / (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);

// Secondary puff
constexpr float kfSecondaryPuffTimes = 0.4f * (kfPrimaryPuffEndTime - kfPrimaryPuffStartTime);
constexpr float kfSecondaryPuffIntensity = 0.2f / kfSecondaryPuffTimes;

// Explosion trail
constexpr float kfExplosionTrailWidth = 1.0f;

// Static indices for registered explosion effect types
static uint8_t suiExplosionPointLightTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryLightControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionPuffTypeIndex = kuiInvalidControllerType;
static uint8_t suiPrimaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiSecondaryPuffControllerTypeIndex = kuiInvalidControllerType;
static uint8_t suiExplosionTrailTypeIndex = kuiInvalidTrailType;
static uint8_t suiWindRadialControllerTypeIndex = kuiInvalidControllerType;

// Helper to sync an explosion trail
static void XM_CALLCONV SyncExplosionTrail(game::FrameInterpolate& rFrameInterpolate, smoke_trails_t trailId, FXMVECTOR vecPosition, float fIntensity)
{
	if (!trailId.IsValid())
	{
		return;
	}

	SmokeTrailsInterpolate::Sync(rFrameInterpolate, trailId,
	{
		.vecPosition = vecPosition,
		.fIntensity = fIntensity,
	});
}

#endif // BT_CLIENT

void ExplosionsInterpolate::AllocateAndCopy(ExplosionsInterpolate& rCurrent, const ExplosionsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

#ifdef BT_CLIENT
	// Copy child IDs
	if (rCurrent.iCount > 0)
	{
		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			std::memcpy(rCurrent.pTrails[j], rPrevious.pTrails[j], rCurrent.iCount * sizeof(rCurrent.pTrails[j][0]));
		}
	}
#endif
}

void ExplosionsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ExplosionsInterpolate& rCurrent = rCurrentFrameInterpolate.explosions;
	const ExplosionsInterpolate& rPrevious = rPreviousFrame.interpolate.explosions;

	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rPrevious.puiTypeIndices[i];
		ExplosionFlags_t flags = rPrevious.pFlags[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];

#ifdef BT_CLIENT
		float fLightPercent = rPrevious.pfLightPercents[i];
#endif
		float fSizePercent = rPrevious.pfSizePercents[i];
#ifdef BT_CLIENT
		float fSmokePercent = rPrevious.pfSmokePercents[i];
#endif
		float fTimePercent = rPrevious.pfTimePercents[i];

		int32_t iTrailCount = rPrevious.piTrailCounts[i];

		// Save
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pFlags[i] = flags;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;

#ifdef BT_CLIENT
		rCurrent.pfLightPercents[i] = fLightPercent;
#endif
		rCurrent.pfSizePercents[i] = fSizePercent;
#ifdef BT_CLIENT
		rCurrent.pfSmokePercents[i] = fSmokePercent;
#endif
		rCurrent.pfTimePercents[i] = fTimePercent;

		rCurrent.piTrailCounts[i] = iTrailCount;

		// Copy trail data (not IDs - those are copied in AllocateAndCopy)
		for (int64_t j = 0; j < iTrailCount; ++j)
		{
			rCurrent.pfTrailTimes[j][i] = rPrevious.pfTrailTimes[j][i];
#ifdef BT_CLIENT
			rCurrent.pfTrailIntensities[j][i] = rPrevious.pfTrailIntensities[j][i];
			rCurrent.pVecTrailStartPositions[j][i] = rPrevious.pVecTrailStartPositions[j][i];
			rCurrent.pVecTrailEndPositions[j][i] = rPrevious.pVecTrailEndPositions[j][i];
#endif
		}

#ifdef BT_CLIENT
		// Sync trail positions with gravity
		const ExplosionType& rType = sTypes.at(uiTypeIndex);
		float fExplosionTime = fCurrentTime - fStartTime;

		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			smoke_trails_t trailId = rCurrent.pTrails[j][i];
			if (!trailId.IsValid())
			{
				continue;
			}

			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rCurrent.pfTrailTimes[j][i];

			// For expired trails, sync with zero intensity (they'll be removed in Destroy phase)
			if (fExplosionTime >= fTrailEndTime)
			{
				XMVECTOR vecTrailEnd = rCurrent.pVecTrailEndPositions[j][i];
				SyncExplosionTrail(rCurrentFrameInterpolate, trailId, vecTrailEnd, 0.0f);
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

			// Sync trail
			SyncExplosionTrail(rCurrentFrameInterpolate, trailId, vecTrailPosition, fTrailIntensity);
		}
#endif
	}
}

void ExplosionsPostRender::AllocateAndCopy(ExplosionsPostRender& rCurrent, const ExplosionsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void ExplosionsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void ExplosionsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void ExplosionsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void ExplosionsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void ExplosionsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void ExplosionsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void ExplosionsInterpolate::Register()
{
#ifdef BT_CLIENT
	// Guard against double registration
	if (suiExplosionPointLightTypeIndex != kuiInvalidControllerType)
	{
		return;
	}

	// Register PointLights::Type for explosions
	PointLightsInterpolate::RegisterType(suiExplosionPointLightTypeIndex,
	{
		.crc = data::kTexturesBC7ExplosionpngCrc,
		.uiColor = 0xFFFFFFFF,
		.fVisibleArea = kfPrimaryVisibleSize,
		.fVisibleIntensity = kfPrimaryVisibleIntensity,
		.fLightingArea = kfPrimaryLightingSize,
		.fLightingIntensity = kfPrimaryLightingIntensity,
	});

	// Register primary light controller type (3-keyframe: start -> peak -> fade)
	PointLightsInterpolate::RegisterControllerType(suiPrimaryLightControllerTypeIndex,
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
	PointLightsInterpolate::RegisterControllerType(suiSecondaryLightControllerTypeIndex,
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
	PuffsInterpolate::RegisterType(suiExplosionPuffTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
	});

	// Register primary puff controller type (2-keyframe: start -> expand)
	PuffsInterpolate::RegisterControllerType(suiPrimaryPuffControllerTypeIndex,
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
	PuffsInterpolate::RegisterControllerType(suiSecondaryPuffControllerTypeIndex,
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

	// Register SmokeTrails::Type for explosion trails
	SmokeTrailsInterpolate::RegisterType(suiExplosionTrailTypeIndex,
	{
		.crc = 0,
		.uiColor = 0xFFFFFFFF,
		.fWidth = kfExplosionTrailWidth,
	});

	// Register wind radial controller type (2-keyframe: full intensity -> zero)
	WindRadialsInterpolate::RegisterControllerType(suiWindRadialControllerTypeIndex,
	{
		.uiKeyframeCount = 2,
		.bDestroysSelf = true,
		.pfTimes = {0.0f, kfWindDepositDuration, 0.0f, 0.0f},
		.keyframes = {{.fIntensity = 1.0f, .fSize = 1.0f}, {.fIntensity = 0.0f, .fSize = 1.0f}, {}, {}},
	});
#endif
}

#ifdef BT_CLIENT

uint8_t ExplosionsInterpolate::GetPrimaryLightControllerTypeIndex()
{
	return suiPrimaryLightControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetSecondaryLightControllerTypeIndex()
{
	return suiSecondaryLightControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetPrimaryPuffControllerTypeIndex()
{
	return suiPrimaryPuffControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetSecondaryPuffControllerTypeIndex()
{
	return suiSecondaryPuffControllerTypeIndex;
}

uint8_t ExplosionsInterpolate::GetTrailTypeIndex()
{
	return suiExplosionTrailTypeIndex;
}

uint8_t ExplosionsInterpolate::GetWindRadialControllerTypeIndex()
{
	return suiWindRadialControllerTypeIndex;
}

#endif // BT_CLIENT

void ExplosionsPostRender::Spawn(game::Frame& __restrict rFrame, float fCurrentTime, const SpawnInfo& rInfo)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(rInfo.uiTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	// Initialize explosion data
	rInterpolate.puiTypeIndices[iSpawnIndex] = rInfo.uiTypeIndex;
	rInterpolate.pFlags[iSpawnIndex] = rInfo.flags;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
	rInterpolate.pVecPositions[iSpawnIndex] = rInfo.vecPosition;
	rInterpolate.pVecDirections[iSpawnIndex] = rInfo.vecDirection;

#ifdef BT_CLIENT
	rInterpolate.pfLightPercents[iSpawnIndex] = rInfo.fLightPercent;
#endif
	rInterpolate.pfSizePercents[iSpawnIndex] = rInfo.fSizePercent;
#ifdef BT_CLIENT
	rInterpolate.pfSmokePercents[iSpawnIndex] = rInfo.fSmokePercent;
#endif
	rInterpolate.pfTimePercents[iSpawnIndex] = rInfo.fTimePercent;

	rInterpolate.piTrailCounts[iSpawnIndex] = static_cast<int32_t>(std::min(rInfo.uiTrailCount, static_cast<uint32_t>(kiMaxExplosionTrails)));

	// Initialize trail arrays to invalid
	for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
	{
		rInterpolate.pfTrailTimes[j][iSpawnIndex] = 0.0f;
#ifdef BT_CLIENT
		rInterpolate.pTrails[j][iSpawnIndex] = smoke_trails_t {};
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = 0.0f;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = XMVectorZero();
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = XMVectorZero();
#endif
	}

	// Fire-and-forget effects: Primary light
	if (rType.uiPrimaryLightControllerTypeIndex != kuiInvalidControllerType)
	{
		float fPrimaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
#ifdef BT_CLIENT
		PointLightsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryLightControllerTypeIndex, rInfo.vecPosition, fPrimaryRotation);
#endif
	}

	// Fire-and-forget effects: Primary puff
	if (rType.uiPrimaryPuffControllerTypeIndex != kuiInvalidControllerType)
	{
#ifdef BT_CLIENT
		PuffsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryPuffControllerTypeIndex, rInfo.vecPosition);
#endif
	}

	// Fire-and-forget effects: Secondary explosions (staggered)
	int64_t iSecondaryExplosions = static_cast<int64_t>(rType.uiSecondaryExplosionCount);
	float fDelayDelta = iSecondaryExplosions > 0 ? (0.75f * rInfo.fTimePercent * rType.fPrimaryTime) / static_cast<float>(iSecondaryExplosions) : 0.0f;
	float fDelay = fDelayDelta;

	for (int64_t k = 0; k < iSecondaryExplosions; ++k, fDelay += fDelayDelta)
	{
		// Calculate secondary explosion position
		XMVECTOR vecSecondaryOffset = XMVector3Rotate(XMVectorSet(rType.fSecondaryPositionMin + std::pow(rInfo.fSizePercent, 1.5f) * common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fSecondaryPositionJitter, 0.0f, 0.0f, 0.0f), XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, common::Random<XM_2PI>(rFrame.postRender.randomEngine)));
		XMVECTOR vecSecondaryPosition = XMVectorAdd(vecSecondaryOffset, rInfo.vecPosition);

		// Secondary light
		if (rType.uiSecondaryLightControllerTypeIndex != kuiInvalidControllerType)
		{
			float fSecondaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
#ifdef BT_CLIENT
			PointLightsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryLightControllerTypeIndex, vecSecondaryPosition, fSecondaryRotation);
#endif
		}

		// Secondary puff
		if (rType.uiSecondaryPuffControllerTypeIndex != kuiInvalidControllerType)
		{
#ifdef BT_CLIENT
			PuffsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryPuffControllerTypeIndex, vecSecondaryPosition);
#endif
		}
	}

	// Fire-and-forget wind deposit (radial, auto-expires)
	if (rType.uiWindRadialControllerTypeIndex != kuiInvalidControllerType)
	{
#ifdef BT_CLIENT
		WindRadialsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiWindRadialControllerTypeIndex, rInfo.vecPosition, gWindDepositExplosionsIntensity.Get() * rInfo.fSizePercent, gWindDepositExplosionsWidth.Get() * rInfo.fSizePercent);
#endif
	}

	// Create trails
	XMVECTOR vecDirection2dNormal = XMVector3Normalize(XMVectorMultiply(XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f), rInfo.vecDirection));
	int32_t iTrailCount = rInterpolate.piTrailCounts[iSpawnIndex];

	for (int32_t j = 0; j < iTrailCount; ++j)
	{
		float fTrailTime = rInfo.fTimePercent * (rType.fTrailTimeMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailTimeRandom);
		float fTrailIntensity = rInfo.fSmokePercent * (rType.fTrailIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailIntensityRandom);

		XMVECTOR vecTrailDirection = vecDirection2dNormal;
		if (j != 0)
		{
			vecTrailDirection = XMVector3Rotate(vecTrailDirection, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, rInfo.fTrailAngle * (common::Random(rFrame.postRender.randomEngine) - 0.5f)));
		}

		XMVECTOR vecTrailStart = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(rType.fTrailStart), rInfo.vecPosition);
		float fTrailLength = rType.fTrailLengthMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailLengthRandom;
		XMVECTOR vecTrailEnd = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(fTrailLength), rInfo.vecPosition);

		rInterpolate.pfTrailTimes[j][iSpawnIndex] = fTrailTime;

#ifdef BT_CLIENT
		// Create trail in SmokeTrails collection (start at full intensity, will fade over time in Sync)
		smoke_trails_t trailId;
		SmokeTrailsPostRender::Add(rFrame, trailId, suiExplosionTrailTypeIndex);

		rInterpolate.pTrails[j][iSpawnIndex] = trailId;
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = fTrailIntensity;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = vecTrailStart;
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = vecTrailEnd;

		// Sync trail after Add()
		SyncExplosionTrail(rFrame.interpolate, trailId, vecTrailStart, fTrailIntensity);
#endif
	}

	// Spawn GPU particles
	uint32_t uiTotalParticles = rInfo.uiParticleCount + rType.uiBaseParticleCount;
	for (uint32_t p = 0; p < uiTotalParticles; ++p)
	{
		XMFLOAT4A f4Position {};
		XMVECTOR vecParticlePosition = common::RandomPositionJitter(rInfo.vecPosition, rType.fParticlePositionJitter, rFrame.postRender.randomEngine);
		XMStoreFloat4A(&f4Position, vecParticlePosition);

		float fVelocityMag = rType.fParticleVelocityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVelocityRandom;
		XMVECTOR vecVelocity = XMVectorMultiply(XMVectorReplicate(fVelocityMag), vecDirection2dNormal);
		vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, -0.5f * rInfo.fParticleAngle + rInfo.fParticleAngle * common::Random(rFrame.postRender.randomEngine)));
		vecVelocity = XMVectorSetZ(vecVelocity, rType.fParticleVerticalVelocityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVerticalVelocityRandom);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecVelocity);

		// Calculate particle color based on flags
		uint32_t uiParticleColor = rType.uiParticleColor;
		if (rInfo.flags & kYellow)
		{
			uiParticleColor |= ((100 + common::Random(25u, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25u, rFrame.postRender.randomEngine)) << 8);
		}
		else if (rInfo.flags & kRed)
		{
			uiParticleColor |= ((50 + common::Random(25u, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25u, rFrame.postRender.randomEngine)) << 8);
		}

		float fParticleIntensity = rType.fParticleIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleIntensityRandom;

#ifdef BT_CLIENT
		if (!(rFrame.interpolate.frameFlags & FrameFlags::kRecalculated))
		{
			ParticleManager::Spawn(gpParticleManager->mLongParticlesSpawnLayout,
			{
				.iColor = static_cast<int32_t>(uiParticleColor),
				.iLightingIntensity = static_cast<int32_t>(rType.fParticleLightingIntensity),
				.fVelocityDecay = rType.fParticleVelocityDecay,
				.fGravity = rType.fParticleGravity,
				.fIntensityDecay = rType.fParticleIntensityDecay,
				.fLightingSize = rType.fParticleLightingSize,
				.fSize = rType.fParticleWidth,
				.fLength = rType.fParticleLength,
				.fIntensity = fParticleIntensity,
				.fIntensityPower = rType.fParticleIntensityPower,
				.f4Position = f4Position,
				.f4Velocity = f4Velocity,
			}, rType.particleCrc);
		}
#endif
	}
}

void ExplosionsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	ExplosionsInterpolate& rInterpolate = rFrame.interpolate.explosions;
	ExplosionsPostRender& rPostRender = rFrame.postRender.explosions;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiTypeIndex = rInterpolate.puiTypeIndices[i];
		const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(uiTypeIndex);

		float fStartTime = rInterpolate.pfStartTimes[i];
		float fExplosionTime = fCurrentTime - fStartTime;
		float fTimePercent = rInterpolate.pfTimePercents[i];

		int32_t iTrailCount = rInterpolate.piTrailCounts[i];

#ifdef BT_CLIENT
		// Remove expired trails (cleanup happens every frame, not just at explosion expiration)
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			smoke_trails_t& trailId = rInterpolate.pTrails[j][i];
			if (!trailId.IsValid())
			{
				continue;
			}

			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			if (fExplosionTime >= fTrailEndTime)
			{
				SmokeTrailsPostRender::Remove(rFrame, trailId);
			}
		}
#endif

		ExplosionFlags_t flags = rInterpolate.pFlags[i];

		// Skip non-self-destroying explosions for full removal
		if (!(flags & kDestroysSelf))
		{
			continue;
		}

		// Calculate end time (longest of trail durations)
		float fEndTime = 0.0f;
		for (int32_t j = 0; j < iTrailCount; ++j)
		{
			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + rInterpolate.pfTrailTimes[j][i];
			fEndTime = std::max(fEndTime, fTrailEndTime);
		}

		// Check if explosion has expired
		if (fExplosionTime < fEndTime)
		{
			continue;
		}

		// Remove the explosion using swap-and-pop
		DestroyElement(rInterpolate, rPostRender, i, rInterpolate.Members(), rPostRender.Members());
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

#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(pfLightPercents[i], rOther.pfLightPercents[i]);
#endif
		bEqual &= common::BreakOnNotEqual(pfSizePercents[i], rOther.pfSizePercents[i]);
#ifdef BT_CLIENT
		bEqual &= common::BreakOnNotEqual(pfSmokePercents[i], rOther.pfSmokePercents[i]);
#endif
		bEqual &= common::BreakOnNotEqual(pfTimePercents[i], rOther.pfTimePercents[i]);

		bEqual &= common::BreakOnNotEqual(piTrailCounts[i], rOther.piTrailCounts[i]);

		for (int64_t j = 0; j < piTrailCounts[i]; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pfTrailTimes[j][i], rOther.pfTrailTimes[j][i]);
#ifdef BT_CLIENT
			bEqual &= common::BreakOnNotEqual(pTrails[j][i], rOther.pTrails[j][i]);
			bEqual &= common::BreakOnNotEqual(pfTrailIntensities[j][i], rOther.pfTrailIntensities[j][i]);
			bEqual &= common::BreakOnNotEqual(pVecTrailStartPositions[j][i], rOther.pVecTrailStartPositions[j][i]);
			bEqual &= common::BreakOnNotEqual(pVecTrailEndPositions[j][i], rOther.pVecTrailEndPositions[j][i]);
#endif
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

#ifdef BT_CLIENT

static int64_t siTotalCount = 0;

void ExplosionsInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	siTotalCount = 0;
}

void ExplosionsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	siTotalCount += rFrameInterpolate.explosions.iCount;
}

void ExplosionsInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterExplosions, siTotalCount);
}

#endif // BT_CLIENT

} // namespace engine
