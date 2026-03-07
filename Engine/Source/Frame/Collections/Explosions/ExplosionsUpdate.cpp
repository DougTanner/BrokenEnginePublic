#include "Explosions.h"

namespace engine
{

#ifdef BT_CLIENT

// Forward declaration for shared helper (defined in Explosions.cpp)
void XM_CALLCONV SyncExplosionTrail(game::FrameInterpolate& rFrameInterpolate, smoke_trails_t trailId, FXMVECTOR vecPosition, float fIntensity);

#endif // BT_CLIENT

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
		for (int64_t j = 0; j < kiMaxExplosionTrails; ++j)
		{
			if (j < iTrailCount)
			{
				rCurrent.pfTrailTimes[j][i] = rPrevious.pfTrailTimes[j][i];
#ifdef BT_CLIENT
				rCurrent.pfTrailIntensities[j][i] = rPrevious.pfTrailIntensities[j][i];
				rCurrent.pVecTrailStartPositions[j][i] = rPrevious.pVecTrailStartPositions[j][i];
				rCurrent.pVecTrailEndPositions[j][i] = rPrevious.pVecTrailEndPositions[j][i];
#endif
			}
			else
			{
				rCurrent.pfTrailTimes[j][i] = 0.0f;
#ifdef BT_CLIENT
				rCurrent.pfTrailIntensities[j][i] = 0.0f;
				rCurrent.pVecTrailStartPositions[j][i] = XMVectorZero();
				rCurrent.pVecTrailEndPositions[j][i] = XMVectorZero();
#endif
			}
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

} // namespace engine
