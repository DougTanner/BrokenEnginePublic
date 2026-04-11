#include "Spaceships.h"

#include "Frame/FrameStaticData.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Pushers/Pushers.h"

namespace game
{

using enum SpaceshipFlags;

// Spaceship Ai
constexpr float kfAccelerationTowardsPlayer = 4.0f;
constexpr float kfFleePlayerAcceleration = 6.0f;
constexpr float kfReturnToIslandCenterAcceleration = kfSpaceshipMaxAcceleration;
constexpr float kfDeltaAngleChange = 0.999f;
constexpr float kfDeltaAngleDecay = 6.0f;
constexpr float kfDeltaAngleTowardsPlayer = 32.0f;
constexpr float kfFleePlayerDeltaAngle = 32.0f;
constexpr float kfFleePlayerStart = 15.0f;
constexpr float kfFleePlayerEnd = 25.0f;
constexpr float kfReturnDistance = 180.0f;
constexpr float kfReturnedDistance = kfReturnDistance - 20.0f;
constexpr float kfVelocityToDirection = 4.0f;
constexpr float kfTerrainCollisionRotation = 8.0f;
constexpr float kfTerrainCollisionMovePosition = kfSpaceshipRadius * 2.0f;
constexpr float kfTerrainCollisionAddVelocity = 4.0f;

// Terrain avoidance (player-proximity skip constants; sampling constants in GameUtils.cpp)
constexpr float kfIgnoreAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfIgnoreAvoidTerrainPlayerDistance = 40.0f;

// Forward declarations for shared helpers (defined in Spaceships.cpp)
[[nodiscard]] bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult);

void XM_CALLCONV SpaceshipsPostRender::ComputeSteering(FXMVECTOR vecPosition, FXMVECTOR vecDirection, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, float fDeltaTime, SpaceshipFlags_t& rFlags, float& rfDeltaRotation)
{
	XMVECTOR vecToPlayer = bPlayerAlive ? XMVectorSubtract(vecNearestPlayer, vecPosition) : XMVectorZero();
	float fPlayerDistance = bPlayerAlive ? XMVectorGetX(XMVector3Length(vecToPlayer)) : kfFleePlayerEnd + 1.0f;
	if (fPlayerDistance < kfFleePlayerStart)
	{
		rFlags.Set(kFleePlayer);
	}
	else if (fPlayerDistance > kfFleePlayerEnd)
	{
		rFlags.Clear(kFleePlayer);
	}

	XMVECTOR vecIslandCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	float fDistanceFromIslandCenter = common::Distance(vecPosition, vecIslandCenter);
	if (fDistanceFromIslandCenter > kfReturnDistance)
	{
		rFlags.Set(kReturnToIslandCenter);
	}
	else if (fDistanceFromIslandCenter < kfReturnedDistance)
	{
		rFlags.Clear(kReturnToIslandCenter);
	}

	XMVECTOR vecDestination = bPlayerAlive ? vecNearestPlayer : vecIslandCenter;
	if (rFlags & kReturnToIslandCenter)
	{
		vecDestination = vecIslandCenter;
	}
	XMVECTOR vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(vecDestination, vecPosition));
	float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(vecDirection, vecToDestinationNormal));
	float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfDeltaAngleTowardsPlayer : -kfDeltaAngleTowardsPlayer;
	if (!(rFlags & kReturnToIslandCenter) && rFlags & kFleePlayer)
	{
		fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? -kfFleePlayerDeltaAngle : kfFleePlayerDeltaAngle;
	}

	rfDeltaRotation = kfDeltaAngleChange * rfDeltaRotation + (1.0f - kfDeltaAngleChange) * fWantedDeltaRotation;
	rfDeltaRotation = common::ExponentialDecay(kfDeltaAngleDecay, fDeltaTime) * rfDeltaRotation;
}

void XM_CALLCONV SpaceshipsPostRender::ApplyMovement(Frame& __restrict rFrame, const SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, SpaceshipFlags_t flags, float fDeltaTime, XMVECTOR& rVecVelocity)
{
	// Decay velocity
	rVecVelocity = XMVectorMultiply(XMVectorReplicate(common::ExponentialDecay(kfSpaceshipVelocityDecay, fDeltaTime)), rVecVelocity);

	// Accelerate and rotate velocity
	float fAcceleration = flags & kReturnToIslandCenter ? kfReturnToIslandCenterAcceleration : flags & kFleePlayer ? kfFleePlayerAcceleration : kfAccelerationTowardsPlayer;
	rVecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * fAcceleration), rCurrentInterpolate.pVecDirections[i], rVecVelocity);

	// Blend velocity direction toward facing direction
	float fPercent = 1.0f - fDeltaTime * kfVelocityToDirection;
	XMVECTOR vecVelocityComponent = XMVectorMultiply(XMVectorReplicate(fPercent), XMVector3Normalize(rVecVelocity));
	XMVECTOR vecDirectionComponent = XMVectorMultiply(XMVectorReplicate(1.0f - fPercent), rCurrentInterpolate.pVecDirections[i]);
	rVecVelocity = XMVectorMultiply(XMVector3Length(rVecVelocity), XMVector3Normalize(XMVectorAdd(vecVelocityComponent, vecDirectionComponent)));

	// Apply push from nearby pushers (pass own pusher ID to ignore self-push)
	XMVECTOR vecPush = engine::PushersInterpolate::ApplyPush(rFrame.interpolate, rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.puiPushers[i]);
	float fPushLength = XMVectorGetX(XMVector3Length(vecPush));
	if (fPushLength > 0.0f)
	{
		XMVECTOR vecPushDirection = XMVectorDivide(vecPush, XMVectorReplicate(fPushLength));
		rVecVelocity = engine::ApplyClampedPush(rVecVelocity, vecPushDirection, fPushLength, kfSpaceshipMaxPusherPushVelocity);
	}
}

void SpaceshipsPostRender::ApplyTerrainBounce(SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, float fDeltaTime, float& rfDeltaRotation, XMVECTOR& rVecVelocity)
{
	float fTerrainElevation = engine::gpIslandTerrain->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
	if (fTerrainElevation >= XMVectorGetZ(rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
	{
		XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->GlobalNormal(rCurrentInterpolate.pVecPositions[i]), 0.0f));

		rCurrentInterpolate.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionMovePosition), vecTerrainNormal, rCurrentInterpolate.pVecPositions[i]);

		float fDirectionTerrainCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecTerrainNormal));
		rfDeltaRotation = fDirectionTerrainCrossZ > 0.0f ? kfTerrainCollisionRotation : -kfTerrainCollisionRotation;

		rVecVelocity = XMVector3Reflect(rVecVelocity, vecTerrainNormal);
		rVecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfTerrainCollisionAddVelocity), vecTerrainNormal, rVecVelocity);
	}
}

void SpaceshipsPostRender::AvoidTerrain([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, int64_t iStart, int64_t iEnd)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		// Skip terrain avoidance if close to nearest alive player and facing them
		XMVECTOR vecNearestPlayer = XMVectorZero();
		if (NearestAlivePlayerPosition(*rFrame.interpolate.pPlayers, *rFrame.postRender.pPlayers, rCurrentInterpolate.pVecPositions[i], vecNearestPlayer))
		{
			XMVECTOR vecToPlayer = XMVectorSubtract(vecNearestPlayer, rCurrentInterpolate.pVecPositions[i]);
			float fDistanceToPlayer = XMVectorGetX(XMVector3Length(vecToPlayer));
			if (fDistanceToPlayer < kfIgnoreAvoidTerrainPlayerDistance)
			{
				XMVECTOR vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
				float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrentInterpolate.pVecDirections[i], vecToPlayerNormal));
				if (fAngleToPlayer < kfIgnoreAvoidTerrainPlayerAngle)
				{
					continue;
				}
			}
		}

		rCurrentInterpolate.pfDeltaRotations[i] = ComputeTerrainAvoidance(rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.pVecDirections[i], rCurrentInterpolate.pfDeltaRotations[i]);

		// Clamp delta rotation
		rCurrentInterpolate.pfDeltaRotations[i] = common::MinAbs(rCurrentInterpolate.pfDeltaRotations[i], kfDeltaAngleMax);
	}
}

} // namespace game
