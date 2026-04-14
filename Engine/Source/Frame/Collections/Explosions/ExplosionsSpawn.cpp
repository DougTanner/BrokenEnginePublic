#include "Explosions.h"

#if defined(BT_CLIENT)
#include "Data/Texture.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Frame/Collections/SmokeTrails/SmokeTrails.h"
#include "Frame/Collections/WindRadials/WindRadials.h"
#include "Ui/LightingWrappers.h"
#endif // BT_CLIENT

namespace engine
{

using enum ExplosionFlags;

#if defined(BT_CLIENT)

// Forward declaration for shared helper (defined in Explosions.cpp)
void XM_CALLCONV SyncExplosionTrail(game::FrameInterpolate& rFrameInterpolate, smoke_trails_t trailId, FXMVECTOR vecPosition, float fIntensity);

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
	rInterpolate.pVecPositions[iSpawnIndex] = XMVectorSetW(rInfo.vecPosition, 1.0f);
	rInterpolate.pVecDirections[iSpawnIndex] = rInfo.vecDirection;

#if defined(BT_CLIENT)
	rInterpolate.pfLightPercents[iSpawnIndex] = rInfo.fLightPercent;
#endif
	rInterpolate.pfSizePercents[iSpawnIndex] = rInfo.fSizePercent;
#if defined(BT_CLIENT)
	rInterpolate.pfSmokePercents[iSpawnIndex] = rInfo.fSmokePercent;
#endif
	rInterpolate.pfTimePercents[iSpawnIndex] = rInfo.fTimePercent;

	rInterpolate.piTrailCounts[iSpawnIndex] = static_cast<int32_t>(std::min(rInfo.uiTrailCount, static_cast<uint32_t>(kiMaxExplosionTrails)));

	// Initialize trail arrays to invalid
	for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
	{
		rInterpolate.pfTrailTimes[j][iSpawnIndex] = 0.0f;
#if defined(BT_CLIENT)
		rInterpolate.pTrails[j][iSpawnIndex] = smoke_trails_t {};
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = 0.0f;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = XMVectorZero();
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = XMVectorZero();
#endif
	}

	// Consume random unconditionally to keep random engine in sync across client/server
	[[maybe_unused]] float fPrimaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);

	// Fire-and-forget effects: Primary light
#if defined(BT_CLIENT)
	if (rType.uiPrimaryLightControllerTypeIndex != kuiInvalidControllerType)
	{
		PointLightsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryLightControllerTypeIndex, rInfo.vecPosition, fPrimaryRotation);
	}
#endif

	// Fire-and-forget effects: Primary puff
	if (rType.uiPrimaryPuffControllerTypeIndex != kuiInvalidControllerType)
	{
#if defined(BT_CLIENT)
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

		// Consume random unconditionally to keep random engine in sync across client/server
		[[maybe_unused]] float fSecondaryRotation = common::Random<XM_2PI>(rFrame.postRender.randomEngine);

		// Secondary light
#if defined(BT_CLIENT)
		if (rType.uiSecondaryLightControllerTypeIndex != kuiInvalidControllerType)
		{
			PointLightsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryLightControllerTypeIndex, vecSecondaryPosition, fSecondaryRotation);
		}
#endif

		// Secondary puff
		if (rType.uiSecondaryPuffControllerTypeIndex != kuiInvalidControllerType)
		{
#if defined(BT_CLIENT)
			PuffsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryPuffControllerTypeIndex, vecSecondaryPosition);
#endif
		}
	}

	// Fire-and-forget wind deposit (radial, auto-expires)
	if (rType.uiWindRadialControllerTypeIndex != kuiInvalidControllerType)
	{
#if defined(BT_CLIENT)
		float fWindSizePercent = std::sqrt(rInfo.fSizePercent);
		WindRadialsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiWindRadialControllerTypeIndex, rInfo.vecPosition, game::gWindDepositExplosionsIntensity.Get() * fWindSizePercent, game::gWindDepositExplosionsWidth.Get() * fWindSizePercent);
#endif
	}

	// Create trails
	XMVECTOR vecDirection2dNormal = XMVector3Normalize(XMVectorMultiply(XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f), rInfo.vecDirection));
	int32_t iTrailCount = rInterpolate.piTrailCounts[iSpawnIndex];

	for (int32_t j = 0; j < iTrailCount; ++j)
	{
		float fTrailTime = rInfo.fTimePercent * (rType.fTrailTimeMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailTimeRandom);
		[[maybe_unused]] float fTrailIntensity = rInfo.fSmokePercent * (rType.fTrailIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailIntensityRandom);

		XMVECTOR vecTrailDirection = vecDirection2dNormal;
		if (j != 0)
		{
			vecTrailDirection = XMVector3Rotate(vecTrailDirection, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, rInfo.fTrailAngle * (common::Random(rFrame.postRender.randomEngine) - 0.5f)));
		}

		XMVECTOR vecTrailStart = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(rType.fTrailStart), rInfo.vecPosition);
		float fTrailLength = rType.fTrailLengthMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailLengthRandom;
		XMVECTOR vecTrailEnd = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(fTrailLength), rInfo.vecPosition);

		rInterpolate.pfTrailTimes[j][iSpawnIndex] = fTrailTime;

#if defined(BT_CLIENT)
		// Create trail in SmokeTrails collection (start at full intensity, will fade over time in Sync)
		smoke_trails_t trailId;
		SmokeTrailsPostRender::Add(rFrame, trailId, ExplosionsInterpolate::GetTrailTypeIndex());

		rInterpolate.pTrails[j][iSpawnIndex] = trailId;
		rInterpolate.pfTrailIntensities[j][iSpawnIndex] = fTrailIntensity;
		rInterpolate.pVecTrailStartPositions[j][iSpawnIndex] = vecTrailStart;
		rInterpolate.pVecTrailEndPositions[j][iSpawnIndex] = vecTrailEnd;

		// Sync trail after Add()
		SyncExplosionTrail(rFrame.interpolate, trailId, vecTrailStart, fTrailIntensity);
#endif // BT_CLIENT
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

		[[maybe_unused]] float fParticleIntensity = rType.fParticleIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleIntensityRandom;
#if defined(BT_CLIENT)
		fParticleIntensity *= game::gExplosionParticleLightingIntensity.Get();
#endif // BT_CLIENT

#if defined(BT_CLIENT)
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
#endif // BT_CLIENT
	}
}

} // namespace engine
