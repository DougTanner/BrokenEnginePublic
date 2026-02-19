#include "PlayerAi.h"

#include "Graphics/Islands.h"

#include "Frame/Frame.h"
#include "Frame/Player.h"
#include "Frame/Collections/Spaceships.h"

namespace game
{

using enum PlayerFlags;

constexpr float kfBurstDuration = 0.5f;
constexpr float kfBurstCooldown = 2.0f;
constexpr float kfMissileBurstDuration = 0.4f;
constexpr float kfMissileBurstCooldown = 3.6f;
constexpr float kfTargetRange = 80.0f;

constexpr float kfPreferredElevation = 0.2f;
constexpr float kfElevationCorrectionStrength = 2.0f;
constexpr float kfSteerRate = 3.0f;
constexpr float kfLookAheadDistance = 20.0f;
constexpr float kfHighElevationThreshold = 0.5f;
constexpr float kfUrgentSteerMultiplier = 3.0f;
constexpr float kfMinGradientSq = 0.0001f;
constexpr float kfReturnToIslandDistance = 150.0f;

[[nodiscard]] static bool XM_CALLCONV HasLineOfSight(FXMVECTOR vecFrom, FXMVECTOR vecTo)
{
	constexpr float kfStepInterval = 8.0f;
	constexpr float kfBlockingElevation = 0.4f;

	XMVECTOR vecDelta = XMVectorSubtract(vecTo, vecFrom);
	float fDistance = XMVectorGetX(XMVector3Length(vecDelta));
	int64_t iSteps = static_cast<int64_t>(fDistance / kfStepInterval);
	if (iSteps <= 0)
	{
		return true;
	}

	XMVECTOR vecStep = vecDelta / static_cast<float>(iSteps);
	XMVECTOR vecCurrent = vecFrom;
	for (int64_t k = 1; k < iSteps; ++k)
	{
		vecCurrent = XMVectorAdd(vecCurrent, vecStep);
		if (engine::gpIslands->GlobalElevation(vecCurrent) > kfBlockingElevation)
		{
			return false;
		}
	}
	return true;
}

void PlayerAi::UpdatePlayer(const Frame& rCurrentFrame, int64_t iPlayerIndex, PlayerInput& rPlayerInput)
{
	const PlayersInterpolate& rPlayersInterpolate = rCurrentFrame.interpolate.players;

	// Initialize direction if zero (first spawn or after reset)
	if (XMVectorGetX(XMVector3LengthSq(mVecDirections[iPlayerIndex])) < 0.001f)
	{
		float fAngle = common::Random<XM_2PI>(mRandomEngine);
		mVecDirections[iPlayerIndex] = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
	}

	XMVECTOR vecPosition = rPlayersInterpolate.pVecPositions[iPlayerIndex];
	XMVECTOR vecDirection = XMVector3Normalize(mVecDirections[iPlayerIndex]);

	// Gradient-based contour following
	XMVECTOR vecNormal = engine::gpIslands->GlobalNormal(vecPosition);
	float fNx = XMVectorGetX(vecNormal);
	float fNy = XMVectorGetY(vecNormal);
	float fGradientSq = fNx * fNx + fNy * fNy;

	float fSteerRate = kfSteerRate;
	XMVECTOR vecDesired = XMVectorZero();

	if (fGradientSq > kfMinGradientSq)
	{
		// Contour direction: perpendicular to downhill gradient
		XMVECTOR vecContour = (iPlayerIndex % 2 == 0)
			? XMVectorSet(fNy, -fNx, 0.0f, 0.0f)
			: XMVectorSet(-fNy, fNx, 0.0f, 0.0f);

		// Elevation correction: push toward preferred elevation
		float fElevation = engine::gpIslands->GlobalElevation(vecPosition);
		float fElevationError = fElevation - kfPreferredElevation;
		XMVECTOR vecCorrection = XMVectorScale(XMVectorSet(fNx, fNy, 0.0f, 0.0f), fElevationError * kfElevationCorrectionStrength);

		vecDesired = XMVector3Normalize(XMVectorAdd(vecContour, vecCorrection));

		// Mountain look-ahead: steer faster when high terrain ahead
		XMVECTOR vecAhead = XMVectorAdd(vecPosition, XMVectorScale(vecDirection, kfLookAheadDistance));
		float fElevationAhead = engine::gpIslands->GlobalElevation(vecAhead);
		if (fElevationAhead > kfHighElevationThreshold)
		{
			fSteerRate *= kfUrgentSteerMultiplier;
		}
	}
	else
	{
		// Over open ocean: head toward island center
		vecDesired = XMVector3Normalize(XMVectorNegate(vecPosition));
	}

	// Also return to island if very far from center
	if (common::Distance(vecPosition, XMVectorZero()) > kfReturnToIslandDistance)
	{
		vecDesired = XMVector3Normalize(XMVectorNegate(vecPosition));
		fSteerRate = kfSteerRate * kfUrgentSteerMultiplier;
	}

	// Smooth steering via exponential interpolation
	mVecDirections[iPlayerIndex] = XMVector3Normalize(XMVectorLerp(vecDirection, vecDesired, common::ExponentialInterpolant(fSteerRate, kfDeltaTime)));

	// Find nearest alive spaceship
	const SpaceshipsInterpolate& rSpaceshipsInterpolate = rCurrentFrame.interpolate.spaceships;
	int64_t iSpaceshipCount = rCurrentFrame.postRender.spaceships.iCount;
	float fClosestDistance = kfTargetRange;
	XMVECTOR vecClosestPosition = XMVectorZero();
	bool bTargetFound = false;

	for (int64_t j = 0; j < iSpaceshipCount; ++j)
	{
		if (rSpaceshipsInterpolate.pfDestroyedTimes[j] != -1.0f)
		{
			continue;
		}

		if (!FrameInterpolate::IsVisible(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]))
		{
			continue;
		}

		float fDistance = common::Distance(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]);
		if (fDistance < fClosestDistance)
		{
			if (!HasLineOfSight(vecPosition, rSpaceshipsInterpolate.pVecPositions[j]))
			{
				continue;
			}

			fClosestDistance = fDistance;
			vecClosestPosition = rSpaceshipsInterpolate.pVecPositions[j];
			bTargetFound = true;
		}
	}

	// Manage burst timer
	mfFireTimers[iPlayerIndex] -= kfDeltaTime;
	if (mfFireTimers[iPlayerIndex] <= 0.0f && bTargetFound)
	{
		mfFireTimers[iPlayerIndex] = kfBurstDuration + kfBurstCooldown;
	}

	bool bFiring = mfFireTimers[iPlayerIndex] > kfBurstCooldown && bTargetFound;

	// Manage missile timer
	mfMissileTimers[iPlayerIndex] -= kfDeltaTime;
	if (mfMissileTimers[iPlayerIndex] <= 0.0f && bTargetFound)
	{
		mfMissileTimers[iPlayerIndex] = kfMissileBurstDuration + kfMissileBurstCooldown;
	}

	bool bFiringMissiles = mfMissileTimers[iPlayerIndex] > kfMissileBurstCooldown && bTargetFound;

	// Set inputs
	bool bAiming = bFiring || bFiringMissiles;

	rPlayerInput.f3Move = XMFLOAT3(XMVectorGetX(mVecDirections[iPlayerIndex]), XMVectorGetY(mVecDirections[iPlayerIndex]), 0.0f);
	if (bAiming)
	{
		rPlayerInput.vecDirection = XMVector3Normalize(XMVectorSubtract(vecClosestPosition, vecPosition));
	}
	else
	{
		rPlayerInput.vecDirection = mVecDirections[iPlayerIndex];
	}

	if (bFiring)
	{
		rPlayerInput.flags.Set(FrameInputHeldFlags::kPrimary);
	}

	if (bFiringMissiles)
	{
		rPlayerInput.flags.Set(FrameInputHeldFlags::kSecondary);
	}
}

} // namespace game
