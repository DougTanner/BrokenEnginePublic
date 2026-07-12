#include "TerrainUtils.h"

#include "Frame/FrameStaticData.h"

namespace game
{

// AI contour-following steering constants
constexpr float kfPreferredElevation = 0.2f;
constexpr float kfElevationCorrectionStrength = 2.0f;
constexpr float kfSteerRate = 3.0f;
constexpr float kfLookAheadDistance = 20.0f;
constexpr float kfHighElevationThreshold = 0.5f;
constexpr float kfUrgentSteerMultiplier = 3.0f;
constexpr float kfMinGradientSq = 0.0001f;
constexpr float kfReturnToIslandDistance = 150.0f;

AiSteeringResult XM_CALLCONV ComputeAiSteering(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecCurrentDirection, FXMVECTOR vecFrameCenter, float fDeltaTime, bool bAlternateContour)
{
	XMVECTOR vecDirection = XMVector3Normalize(vecCurrentDirection);

	// Gradient-based contour following
	XMVECTOR vecNormal = engine::gpIslandTerrain->FrameNormal(rStaticData, vecPosition);
	float fNx = XMVectorGetX(vecNormal);
	float fNy = XMVectorGetY(vecNormal);
	float fGradientSq = fNx * fNx + fNy * fNy;

	float fLocalSteerRate = kfSteerRate;
	XMVECTOR vecDesired = XMVectorZero();

	if (fGradientSq > kfMinGradientSq)
	{
		// Contour direction: perpendicular to downhill gradient
		XMVECTOR vecContour = bAlternateContour
			? XMVectorSet(fNy, -fNx, 0.0f, 0.0f)
			: XMVectorSet(-fNy, fNx, 0.0f, 0.0f);

		// Elevation correction: push toward preferred elevation
		float fElevationAi = engine::gpIslandTerrain->FrameElevation(rStaticData, vecPosition);
		float fElevationError = fElevationAi - kfPreferredElevation;
		XMVECTOR vecCorrection = XMVectorScale(XMVectorSet(fNx, fNy, 0.0f, 0.0f), fElevationError * kfElevationCorrectionStrength);

		vecDesired = XMVector3Normalize(XMVectorAdd(vecContour, vecCorrection));

		// Mountain look-ahead: steer faster when high terrain ahead
		XMVECTOR vecAhead = XMVectorAdd(vecPosition, XMVectorScale(vecDirection, kfLookAheadDistance));
		float fElevationAhead = engine::gpIslandTerrain->FrameElevation(rStaticData, vecAhead);
		if (fElevationAhead > kfHighElevationThreshold)
		{
			fLocalSteerRate *= kfUrgentSteerMultiplier;
		}
	}
	else
	{
		// Over open ocean: head toward island center
		vecDesired = XMVector3Normalize(XMVectorSubtract(vecFrameCenter, vecPosition));
	}

	// Return to island if very far from center
	if (common::Distance(vecPosition, vecFrameCenter) > kfReturnToIslandDistance)
	{
		vecDesired = XMVector3Normalize(XMVectorSubtract(vecFrameCenter, vecPosition));
		fLocalSteerRate = kfSteerRate * kfUrgentSteerMultiplier;
	}

	// Smooth steering via exponential interpolation
	XMVECTOR vecAiDirection = XMVector3Normalize(XMVectorLerp(vecDirection, vecDesired, common::ExponentialInterpolant(fLocalSteerRate, fDeltaTime)));

	return {vecAiDirection};
}

// Terrain avoidance sampling constants
constexpr int64_t kiFrontSamples = 4;
constexpr float kfFrontSamplesStep = 4.0f;
constexpr int64_t kiSideSamples = 2;
constexpr float kfSideSamplesStep = 2.0f;
constexpr float kfStepReduceWeight = 0.1f;
constexpr float kfAvoidTerrainMin = 0.5f;
constexpr float kfAvoidTerrainMax = 2.5f;
constexpr float kfAvoidTerrainDeltaAngleMin = 16.0f;
constexpr float kfAvoidTerrainDeltaAngleMax = 32.0f;
constexpr float kfDeltaAngleChangeAvoidTerrain = 0.995f;

float XM_CALLCONV ComputeTerrainAvoidance(const engine::FrameStaticData& rStaticData, FXMVECTOR vecPosition, FXMVECTOR vecDirection, float fCurrentDeltaRotation)
{
	// Sample terrain elevation in front and to sides. Every sample below is in this one cell's grid, so
	// build the elevation sampler once (hoisting the per-cell origin compute + empty check out of the
	// nested sample loop); Sample() is bit-identical to FrameElevation per sample.
	engine::FrameElevationSampler sampler = engine::gpIslandTerrain->MakeFrameElevationSampler(rStaticData);

	XMVECTOR vecLeftDirection = XMVector3Cross(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), vecDirection);
	float fLeftElevation = 0.0f;
	float fRightElevation = 0.0f;
	float fTotalWeight = 0.0f;

	for (int64_t j = 0; j < kiFrontSamples; ++j)
	{
		float fWeightFront = 1.0f - static_cast<float>(j) * kfStepReduceWeight;
		XMVECTOR vecSamplePosition = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(j + 1) * kfFrontSamplesStep), vecDirection, vecPosition);

		for (int64_t k = 0; k < kiSideSamples; ++k)
		{
			float fWeight = fWeightFront - static_cast<float>(k) * kfStepReduceWeight;
			fTotalWeight += fWeight;

			XMVECTOR vecSamplePositionLeft = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
			fLeftElevation += fWeight * sampler.Sample(vecSamplePositionLeft);

			XMVECTOR vecSamplePositionRight = XMVectorMultiplyAdd(XMVectorReplicate(static_cast<float>(k + 1) * -kfSideSamplesStep), vecLeftDirection, vecSamplePosition);
			fRightElevation += fWeight * sampler.Sample(vecSamplePositionRight);
		}
	}

	float fTotalWeightInverse = 1.0f / fTotalWeight;
	fLeftElevation *= fTotalWeightInverse;
	fRightElevation *= fTotalWeightInverse;

	// Adjust rotation to avoid terrain
	if (fLeftElevation > kfAvoidTerrainMin || fRightElevation > kfAvoidTerrainMin)
	{
		float fPercent = fLeftElevation > fRightElevation ? (fLeftElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax : (fRightElevation - kfAvoidTerrainMin) / kfAvoidTerrainMax;
		fPercent = std::clamp(fPercent, 0.0f, 1.0f);

		float fAvoidDeltaAngle = (1.0f - fPercent) * kfAvoidTerrainDeltaAngleMin + fPercent * kfAvoidTerrainDeltaAngleMax;
		float fWantedDeltaRotation = fLeftElevation > fRightElevation ? -fAvoidDeltaAngle : fAvoidDeltaAngle;
		fCurrentDeltaRotation = kfDeltaAngleChangeAvoidTerrain * fCurrentDeltaRotation + (1.0f - kfDeltaAngleChangeAvoidTerrain) * fWantedDeltaRotation;
	}

	return fCurrentDeltaRotation;
}

SegmentHit XM_CALLCONV TracePointAgainstTerrain(const engine::FrameStaticData& rStaticData, FXMVECTOR vecStartPosition, FXMVECTOR vecEndPosition, float fStartTime, float fEndTime)
{
	static constexpr int64_t kiGridDimension = Frame::kiElevationGridDim;
	static constexpr float kfGridPitchX = Frame::kfCellWidth / static_cast<float>(kiGridDimension);
	static constexpr float kfGridPitchY = Frame::kfCellHeight / static_cast<float>(kiGridDimension);

	engine::FrameElevationSampler sampler = engine::gpIslandTerrain->MakeFrameElevationSampler(rStaticData);
	XMFLOAT4A f4Start {};
	XMFLOAT4A f4End {};
	XMStoreFloat4A(&f4Start, vecStartPosition);
	XMStoreFloat4A(&f4End, vecEndPosition);

	float fDeltaX = f4End.x - f4Start.x;
	float fDeltaY = f4End.y - f4Start.y;
	float fDeltaZ = f4End.z - f4Start.z;
	int64_t iGridX = static_cast<int64_t>(std::floor((f4Start.x - sampler.fCellOriginX) / kfGridPitchX));
	int64_t iGridY = static_cast<int64_t>(std::floor((f4Start.y - sampler.fCellOriginY) / kfGridPitchY));
	int64_t iStepX = fDeltaX > 0.0f ? 1 : fDeltaX < 0.0f ? -1 : 0;
	int64_t iStepY = fDeltaY > 0.0f ? 1 : fDeltaY < 0.0f ? -1 : 0;
	int64_t iNextBoundaryX = iGridX + (iStepX > 0 ? 1 : 0);
	int64_t iNextBoundaryY = iGridY + (iStepY > 0 ? 1 : 0);
	float fAbsoluteDeltaX = std::abs(fDeltaX);
	float fAbsoluteDeltaY = std::abs(fDeltaY);

	float fCurrentPercent = 0.0f;
	for (;;)
	{
		XMVECTOR vecCurrentPosition = XMVectorLerp(vecStartPosition, vecEndPosition, fCurrentPercent);
		float fPointElevation = sampler.Sample(vecCurrentPosition);
		float fCurrentZ = f4Start.z + fDeltaZ * fCurrentPercent;
		if (fCurrentZ <= fPointElevation)
		{
			float fTime = fStartTime + fCurrentPercent * (fEndTime - fStartTime);
			return
			{
				.bHit = true,
				.fTime = fTime,
				.vecPosition = XMVectorSetZ(vecCurrentPosition, fPointElevation),
			};
		}

		float fDistanceX = std::numeric_limits<float>::max();
		float fDistanceY = std::numeric_limits<float>::max();
		if (iStepX != 0)
		{
			float fBoundaryX = sampler.fCellOriginX + static_cast<float>(iNextBoundaryX) * kfGridPitchX;
			fDistanceX = std::abs(fBoundaryX - f4Start.x);
		}
		if (iStepY != 0)
		{
			float fBoundaryY = sampler.fCellOriginY + static_cast<float>(iNextBoundaryY) * kfGridPitchY;
			fDistanceY = std::abs(fBoundaryY - f4Start.y);
		}

		bool bAdvanceX = false;
		bool bAdvanceY = false;
		if (iStepX == 0 && iStepY != 0)
		{
			bAdvanceY = true;
		}
		else if (iStepY == 0 && iStepX != 0)
		{
			bAdvanceX = true;
		}
		else if (iStepX != 0 && iStepY != 0)
		{
			// Compare the two rational crossing times by cross multiplication. A double exactly holds
			// the product of two floats, so mathematical corner ties advance both axes without epsilon.
			double fScaledDistanceX = static_cast<double>(fDistanceX) * static_cast<double>(fAbsoluteDeltaY);
			double fScaledDistanceY = static_cast<double>(fDistanceY) * static_cast<double>(fAbsoluteDeltaX);
			bAdvanceX = fScaledDistanceX <= fScaledDistanceY;
			bAdvanceY = fScaledDistanceY <= fScaledDistanceX;
		}

		float fNextPercent = 1.0f;
		if (bAdvanceX || bAdvanceY)
		{
			fNextPercent = bAdvanceX ? fDistanceX / fAbsoluteDeltaX : fDistanceY / fAbsoluteDeltaY;
			fNextPercent = std::min(1.0f, fNextPercent);
		}
		float fElevation = sampler.fSeaFloor;
		if (sampler.pGrid != nullptr && iGridX >= 0 && iGridX < kiGridDimension && iGridY >= 0 && iGridY < kiGridDimension)
		{
			fElevation = sampler.pGrid->at(static_cast<size_t>(iGridY * kiGridDimension + iGridX));
		}
		// The traversed cell owns the open interval after fCurrentPercent. If a height step rises immediately
		// across a boundary, collision begins at the boundary even when the exact point belongs to the
		// outgoing cell under floor ownership.
		if (fCurrentZ <= fElevation)
		{
			float fTime = fStartTime + fCurrentPercent * (fEndTime - fStartTime);
			XMVECTOR vecPosition = XMVectorSetZ(vecCurrentPosition, fElevation);
			return
			{
				.bHit = true,
				.fTime = fTime,
				.vecPosition = vecPosition,
			};
		}

		float fNextZ = f4Start.z + fDeltaZ * fNextPercent;
		// The next boundary is excluded from this cell. Equality is resolved below by Sample(), whose
		// floor lookup defines ownership for positive crossings and final endpoints.
		if (fNextZ < fElevation)
		{
			float fHitPercent = (fElevation - f4Start.z) / fDeltaZ;
			float fTime = fStartTime + fHitPercent * (fEndTime - fStartTime);
			XMVECTOR vecPosition = XMVectorSetZ(XMVectorLerp(vecStartPosition, vecEndPosition, fHitPercent), fElevation);
			return
			{
				.bHit = true,
				.fTime = fTime,
				.vecPosition = vecPosition,
			};
		}

		if (fNextPercent >= 1.0f)
		{
			float fEndElevation = sampler.Sample(vecEndPosition);
			if (f4End.z <= fEndElevation)
			{
				return
				{
					.bHit = true,
					.fTime = fEndTime,
					.vecPosition = XMVectorSetZ(vecEndPosition, fEndElevation),
				};
			}
			return {};
		}

		if (bAdvanceX)
		{
			iGridX += iStepX;
			iNextBoundaryX += iStepX;
		}
		if (bAdvanceY)
		{
			iGridY += iStepY;
			iNextBoundaryY += iStepY;
		}
		fCurrentPercent = fNextPercent;
	}
}

SegmentHit XM_CALLCONV TracePointToFrameExit(FXMVECTOR vecArea, FXMVECTOR vecStartPosition, FXMVECTOR vecEndPosition, float fStartTime, float fEndTime)
{
	FrameBounds bounds = ComputeFrameBounds(vecArea);
	XMFLOAT4A f4Start {};
	XMFLOAT4A f4End {};
	XMStoreFloat4A(&f4Start, vecStartPosition);
	XMStoreFloat4A(&f4End, vecEndPosition);
	float fDeltaX = f4End.x - f4Start.x;
	float fDeltaY = f4End.y - f4Start.y;
	float fPercent = std::numeric_limits<float>::max();

	bool bStartsOutside = f4Start.x < bounds.fMinX || f4Start.x > bounds.fMaxX || f4Start.y < bounds.fMinY || f4Start.y > bounds.fMaxY;
	bool bStartsOnNonInwardBoundary =
		(f4Start.x == bounds.fMinX && fDeltaX <= 0.0f) ||
		(f4Start.x == bounds.fMaxX && fDeltaX >= 0.0f) ||
		(f4Start.y == bounds.fMinY && fDeltaY <= 0.0f) ||
		(f4Start.y == bounds.fMaxY && fDeltaY >= 0.0f);
	if (bStartsOutside || bStartsOnNonInwardBoundary)
	{
		fPercent = 0.0f;
	}
	else
	{
		if (fDeltaX > 0.0f && f4End.x >= bounds.fMaxX)
		{
			fPercent = std::min(fPercent, (bounds.fMaxX - f4Start.x) / fDeltaX);
		}
		else if (fDeltaX < 0.0f && f4End.x <= bounds.fMinX)
		{
			fPercent = std::min(fPercent, (bounds.fMinX - f4Start.x) / fDeltaX);
		}
		if (fDeltaY > 0.0f && f4End.y >= bounds.fMaxY)
		{
			fPercent = std::min(fPercent, (bounds.fMaxY - f4Start.y) / fDeltaY);
		}
		else if (fDeltaY < 0.0f && f4End.y <= bounds.fMinY)
		{
			fPercent = std::min(fPercent, (bounds.fMinY - f4Start.y) / fDeltaY);
		}
	}

	if (fPercent == std::numeric_limits<float>::max())
	{
		return {};
	}

	return
	{
		.bHit = true,
		.fTime = fStartTime + fPercent * (fEndTime - fStartTime),
		.vecPosition = XMVectorLerp(vecStartPosition, vecEndPosition, fPercent),
	};
}

} // namespace game
