#include "Explosions.h"

#include "Ui/WrapperBase.h"

#if defined(BT_CLIENT)
#include "Data/Texture.h"
#include "Graphics/Managers/ParticleManager.h"
#include "Frame/Collections/PointLights/PointLights.h"
#include "Frame/Collections/Puffs/Puffs.h"
#include "Frame/Collections/SmokeTrails/SmokeTrails.h"
#include "Frame/Collections/WindRadials/WindRadials.h"
#include "Ui/LightingWrappers.h"
#include "Ui/ParticleWrappers.h"
#include "Ui/SmokeWrappers.h"
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

	common::ValidateVector<true >(rInfo.vecPosition);
	common::ValidateVector<false>(rInfo.vecDirection);

	const ExplosionType& rType = ExplosionsInterpolate::sTypes.at(rInfo.uiTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	// Initialize explosion data
	rInterpolate.puiTypeIndices[iSpawnIndex] = rInfo.uiTypeIndex;
	rInterpolate.pFlags[iSpawnIndex] = rInfo.flags;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
	rInterpolate.pVecPositions[iSpawnIndex] = rInfo.vecPosition;
	rInterpolate.pVecDirections[iSpawnIndex] = rInfo.vecDirection;

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
#if defined(BT_CLIENT)
	if (rType.uiPrimaryPuffControllerTypeIndex != kuiInvalidControllerType)
	{
		PuffsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiPrimaryPuffControllerTypeIndex, rInfo.vecPosition);
	}
#endif

	// Fire-and-forget effects: Secondary explosions (staggered)
	int64_t iSecondaryExplosions = static_cast<int64_t>(rType.uiSecondaryExplosionCount);
	float fDelayDelta = iSecondaryExplosions > 0 ? (0.75f * rInfo.fTimePercent * rType.fPrimaryTime) / static_cast<float>(iSecondaryExplosions) : 0.0f;
	float fDelay = fDelayDelta;

	for (int64_t k = 0; k < iSecondaryExplosions; ++k, fDelay += fDelayDelta)
	{
		// Calculate secondary explosion position
		XMVECTOR vecSecondaryOffset = XMVector3Rotate(XMVectorSet(rType.fSecondaryPositionMin + std::pow(rInfo.fSizePercent, 1.5f) * common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fSecondaryPositionJitter, 0.0f, 0.0f, 0.0f), XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, common::Random<XM_2PI>(rFrame.postRender.randomEngine)));
		[[maybe_unused]] XMVECTOR vecSecondaryPosition = XMVectorAdd(vecSecondaryOffset, rInfo.vecPosition);

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
#if defined(BT_CLIENT)
		if (rType.uiSecondaryPuffControllerTypeIndex != kuiInvalidControllerType)
		{
			PuffsPostRender::AddControlled(rFrame, fCurrentTime + fDelay, rType.uiSecondaryPuffControllerTypeIndex, vecSecondaryPosition);
		}
#endif
	}

	// Fire-and-forget wind deposit (radial, auto-expires)
#if defined(BT_CLIENT)
	if (rType.uiWindRadialControllerTypeIndex != kuiInvalidControllerType)
	{
		float fWindSizePercent = std::sqrt(rInfo.fSizePercent);
		WindRadialsPostRender::AddControlled(rFrame, fCurrentTime, rType.uiWindRadialControllerTypeIndex, rInfo.vecPosition, game::gWindDepositExplosionsIntensity.Get() * fWindSizePercent, game::gWindDepositExplosionsWidth.Get() * fWindSizePercent);
	}
#endif

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

		[[maybe_unused]] XMVECTOR vecTrailStart = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(rType.fTrailStart), rInfo.vecPosition);
		[[maybe_unused]] float fTrailLength = rType.fTrailLengthMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fTrailLengthRandom;

		rInterpolate.pfTrailTimes[j][iSpawnIndex] = fTrailTime;

#if defined(BT_CLIENT)
		// j == 0 is the central trail along the explosion direction; j > 0 are angle-jittered side trails
		const bool bPrimary = (j == 0);
		const float fLengthMul = bPrimary ? game::gExplosionPrimaryTrailLength.Get() : game::gExplosionSecondaryTrailLength.Get();
		const float fDurationMul = bPrimary ? game::gExplosionPrimaryTrailDuration.Get() : game::gExplosionSecondaryTrailDuration.Get();
		fTrailIntensity *= bPrimary ? game::gExplosionPrimaryTrailIntensity.Get() : game::gExplosionSecondaryTrailIntensity.Get();
		// Scaling the head's travel distance by Duration keeps head speed constant when Update later scales
		// pfTrailTimes by the same Duration multiplier — so increasing Duration extends both space and time
		// in lockstep rather than slowing the head into the engine's smoke-decay window.
		fTrailLength *= fLengthMul * fDurationMul;

		XMVECTOR vecTrailEnd = XMVectorMultiplyAdd(vecTrailDirection, XMVectorReplicate(fTrailLength), rInfo.vecPosition);

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

	// Per-type tweak multipliers (Particles tab). Null on server, optional on client.
	auto Scale = [](const Wrapper* pWrapper) { return pWrapper != nullptr ? pWrapper->Get() : 1.0f; };
	const float fPositionJitterScale         = Scale(rType.pParticlePositionJitterScale);
	const float fVelocityBaseScale           = Scale(rType.pParticleVelocityBaseScale);
	const float fVelocitySpreadScale         = Scale(rType.pParticleVelocitySpreadScale);
	const float fVerticalVelocityBaseScale   = Scale(rType.pParticleVerticalVelocityBaseScale);
	const float fVerticalVelocitySpreadScale = Scale(rType.pParticleVerticalVelocitySpreadScale);
	const float fIntensitySpreadScale        = Scale(rType.pParticleIntensitySpreadScale);
	[[maybe_unused]] const float fVisibleIntensityScale = Scale(rType.pParticleVisibleIntensityScale);
	[[maybe_unused]] const float fWidthScale          = Scale(rType.pParticleWidthScale);
	[[maybe_unused]] const float fLengthScale         = Scale(rType.pParticleLengthScale);
	[[maybe_unused]] const float fLengthSpreadScale   = Scale(rType.pParticleLengthSpreadScale);
	[[maybe_unused]] const float fVelocityDecayScale  = Scale(rType.pParticleVelocityDecayScale);
	[[maybe_unused]] const float fGravityScale        = Scale(rType.pParticleGravityScale);
	[[maybe_unused]] const float fIntensityDecayScale = Scale(rType.pParticleIntensityDecayScale);
	[[maybe_unused]] const float fIntensityPowerScale = Scale(rType.pParticleIntensityPowerScale);

	for (uint32_t p = 0; p < uiTotalParticles; ++p)
	{
		XMFLOAT4A f4Position {};
		XMVECTOR vecParticlePosition = common::RandomPositionJitter(rInfo.vecPosition, rType.fParticlePositionJitter * fPositionJitterScale, rFrame.postRender.randomEngine);
		XMStoreFloat4A(&f4Position, vecParticlePosition);

		float fVelocityMag = rType.fParticleVelocityMin * fVelocityBaseScale + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVelocityRandom * fVelocitySpreadScale;
		XMVECTOR vecVelocity = XMVectorMultiply(XMVectorReplicate(fVelocityMag), vecDirection2dNormal);
		vecVelocity = XMVector3Rotate(vecVelocity, XMQuaternionRotationRollPitchYaw(0.0f, 0.0f, -0.5f * rInfo.fParticleAngle + rInfo.fParticleAngle * common::Random(rFrame.postRender.randomEngine)));
		vecVelocity = XMVectorSetZ(vecVelocity, rType.fParticleVerticalVelocityMin * fVerticalVelocityBaseScale + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleVerticalVelocityRandom * fVerticalVelocitySpreadScale);
		XMFLOAT4A f4Velocity {};
		XMStoreFloat4A(&f4Velocity, vecVelocity);

		// Calculate particle color based on flags
		[[maybe_unused]] uint32_t uiParticleColor = rType.uiParticleColor;
		if (rInfo.flags & kYellow)
		{
			uiParticleColor |= ((100 + common::Random(25u, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25u, rFrame.postRender.randomEngine)) << 8);
		}
		else if (rInfo.flags & kRed)
		{
			uiParticleColor |= ((50 + common::Random(25u, rFrame.postRender.randomEngine)) << 16) | ((common::Random(25u, rFrame.postRender.randomEngine)) << 8);
		}

		[[maybe_unused]] float fParticleIntensity = (rType.fParticleIntensityMin + common::Random<1.0f>(rFrame.postRender.randomEngine) * rType.fParticleIntensityRandom * fIntensitySpreadScale) * fVisibleIntensityScale;

		// Per-particle length jitter — multiplicative spread driven by LengthSpread wrapper.
		// Random consumed unconditionally to keep stream in sync across builds.
		[[maybe_unused]] const float fLengthJitter = common::Random<1.0f>(rFrame.postRender.randomEngine);

#if defined(BT_CLIENT)
		if (!(rFrame.interpolate.frameFlags & FrameFlags::kRecalculated))
		{
			ParticleManager::Spawn(gpParticleManager->mLongParticlesSpawnLayout,
			{
				.iColor = static_cast<int32_t>(uiParticleColor),
				.fVelocityDecay = rType.fParticleVelocityDecay * fVelocityDecayScale,
				.fGravity = rType.fParticleGravity * fGravityScale,
				.fIntensityDecay = rType.fParticleIntensityDecay * fIntensityDecayScale,
				.fSize = rType.fParticleWidth * fWidthScale,
				.fLength = rType.fParticleLength * fLengthScale * (1.0f + fLengthJitter * fLengthSpreadScale),
				.fVisibleIntensity = fParticleIntensity,
				.fIntensityPower = rType.fParticleIntensityPower * fIntensityPowerScale,
				.f4Position = f4Position,
				.f4Velocity = f4Velocity,
			}, rType.particleCrc);
		}
#endif // BT_CLIENT
	}
}

} // namespace engine
