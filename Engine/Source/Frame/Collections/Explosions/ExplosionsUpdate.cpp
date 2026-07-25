#include "Explosions.h"

#if defined(BT_CLIENT)
#include "Ui/SmokeWrappers.h"
#endif // BT_CLIENT

namespace engine
{

#if defined(BT_CLIENT)

// Forward declaration for shared helper (defined in Explosions.cpp)
void XM_CALLCONV SyncExplosionTrail(game::FrameInterpolate& rFrameInterpolate, smoke_trails_t trailId, FXMVECTOR vecPosition, float fIntensity);

#endif // BT_CLIENT

void ExplosionsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	ExplosionsInterpolate& rCurrent = rCurrentFrameInterpolate.explosions;
	const ExplosionsInterpolate& rPrevious = rPreviousFrame.interpolate.explosions;

	[[maybe_unused]] float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rCurrentFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiTypeIndex = rPrevious.puiTypeIndices[i];
		ExplosionFlags_t flags = rPrevious.pFlags[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		XMVECTOR vecDirection = rPrevious.pVecDirections[i];

		float fTimePercent = rPrevious.pfTimePercents[i];

		int32_t iTrailCount = rPrevious.piTrailCounts[i];

		// Save
		rCurrent.puiTypeIndices[i] = uiTypeIndex;
		rCurrent.pFlags[i] = flags;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pVecDirections[i] = vecDirection;

		rCurrent.pfTimePercents[i] = fTimePercent;

		rCurrent.piTrailCounts[i] = iTrailCount;

		// Copy trail data (not IDs - those are copied in AllocateAndCopy)
		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			if (j < iTrailCount)
			{
				rCurrent.pfTrailTimes[j][i] = rPrevious.pfTrailTimes[j][i];
#if defined(BT_CLIENT)
				rCurrent.pfTrailIntensities[j][i] = rPrevious.pfTrailIntensities[j][i];
				rCurrent.pVecTrailStartPositions[j][i] = rPrevious.pVecTrailStartPositions[j][i];
				rCurrent.pVecTrailEndPositions[j][i] = rPrevious.pVecTrailEndPositions[j][i];
#endif
			}
			else
			{
				rCurrent.pfTrailTimes[j][i] = 0.0f;
#if defined(BT_CLIENT)
				rCurrent.pfTrailIntensities[j][i] = 0.0f;
				rCurrent.pVecTrailStartPositions[j][i] = XMVectorZero();
				rCurrent.pVecTrailEndPositions[j][i] = XMVectorZero();
#endif
			}
		}

#if defined(BT_CLIENT)
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

			// j == 0 is the central trail along the explosion direction; j > 0 are angle-jittered side trails
			const float fDurationMultiplier = (j == 0) ? game::gExplosionPrimaryTrailDuration.Get() : game::gExplosionSecondaryTrailDuration.Get();
			const float fEffectiveTrailTime = rCurrent.pfTrailTimes[j][i] * fDurationMultiplier;
			float fTrailEndTime = fTimePercent * rType.fTrailDelayTime + fEffectiveTrailTime;

			// For expired trails, sync with zero intensity (they'll be removed in Destroy phase)
			if (fExplosionTime >= fTrailEndTime)
			{
				XMVECTOR vecTrailEnd = rCurrent.pVecTrailEndPositions[j][i];
				SyncExplosionTrail(rCurrentFrameInterpolate, trailId, vecTrailEnd, 0.0f);
				continue;
			}

			// Calculate trail position with gravity
			float fTrailPercent = (fExplosionTime - fTimePercent * rType.fTrailDelayTime) / fEffectiveTrailTime;
			fTrailPercent = std::clamp(fTrailPercent, 0.0f, 1.0f);

			XMVECTOR vecTrailStart = rCurrent.pVecTrailStartPositions[j][i];
			XMVECTOR vecTrailEnd = rCurrent.pVecTrailEndPositions[j][i];
			XMVECTOR vecGravityOffset = XMVectorSet(0.0f, 0.0f, fTrailPercent * rType.fTrailGravity, 0.0f);

			XMVECTOR vecTrailPosition = XMVectorLerp(vecTrailStart, XMVectorSubtract(vecTrailEnd, vecGravityOffset), fTrailPercent);
			float fTrailIntensity = (1.0f - fTrailPercent) * rCurrent.pfTrailIntensities[j][i];

			// Sync trail
			SyncExplosionTrail(rCurrentFrameInterpolate, trailId, vecTrailPosition, fTrailIntensity);
		}
#endif // BT_CLIENT
	}
}

void ExplosionsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine
