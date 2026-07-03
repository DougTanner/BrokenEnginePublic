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

} // namespace game
