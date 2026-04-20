#include "Spaceships.h"

#include "Frame/FrameStaticData.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Pushers/Pushers.h"
#include "Ui/WrapperBase.h"

namespace game
{

using enum SpaceshipFlags;

// Spaceship AI — acceleration per behavior
constexpr float kfSpaceshipChaseAcceleration = 4.0f;
constexpr float kfSpaceshipFleeAcceleration = 6.0f;
constexpr float kfSpaceshipReturnAcceleration = kfSpaceshipAcceleration;

// Steering
constexpr float kfSpaceshipSteeringSmoothing = 3.0f;
constexpr float kfSpaceshipSteeringDecay = 6.0f;
constexpr float kfSpaceshipChaseTurnRate = 32.0f;
constexpr float kfSpaceshipFleeTurnRate = 32.0f;

// Velocity-to-direction blend (airplane constraint)
constexpr float kfSpaceshipVelocityToDirection = 4.0f;

// Flee/return hysteresis
constexpr float kfSpaceshipFleeStartDistance = 15.0f;
constexpr float kfSpaceshipFleeEndDistance = 25.0f;
constexpr float kfSpaceshipReturnDistance = 180.0f;
constexpr float kfSpaceshipReturnedDistance = kfSpaceshipReturnDistance - 20.0f;

// Terrain collision
constexpr float kfSpaceshipTerrainBounceRotation = 8.0f;
constexpr float kfSpaceshipTerrainBounceMove = kfSpaceshipRadius * 2.0f;
constexpr float kfSpaceshipTerrainBounceVelocity = 4.0f;

// Terrain avoidance (player-proximity skip constants; sampling constants in GameUtils.cpp)
constexpr float kfSpaceshipAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfSpaceshipAvoidTerrainPlayerDistance = 40.0f;

// Forward declarations for shared helpers (defined in Spaceships.cpp)
[[nodiscard]] bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult);

void XM_CALLCONV SpaceshipsPostRender::ComputeSteering(FXMVECTOR vecPosition, FXMVECTOR vecDirection, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, float fDeltaTime, SpaceshipFlags_t& rFlags, float& rfDeltaRotation)
{
	// Trap: entry inputs — W included for direction (NaN W propagates silently through Normalize)
	ASSERT(std::isfinite(XMVectorGetX(vecPosition)) && std::isfinite(XMVectorGetY(vecPosition)) && std::isfinite(XMVectorGetZ(vecPosition)));
	ASSERT(std::isfinite(XMVectorGetX(vecDirection)) && std::isfinite(XMVectorGetY(vecDirection)) && std::isfinite(XMVectorGetZ(vecDirection)) && std::isfinite(XMVectorGetW(vecDirection)));
	ASSERT(std::isfinite(rfDeltaRotation));

	XMVECTOR vecToPlayer = bPlayerAlive ? XMVectorSubtract(vecNearestPlayer, vecPosition) : XMVectorZero();
	float fPlayerDistance = bPlayerAlive ? XMVectorGetX(XMVector3Length(vecToPlayer)) : kfSpaceshipFleeEndDistance + 1.0f;
	if (fPlayerDistance < kfSpaceshipFleeStartDistance)
	{
		rFlags.Set(kFleePlayer);
	}
	else if (fPlayerDistance > kfSpaceshipFleeEndDistance)
	{
		rFlags.Clear(kFleePlayer);
	}

	XMVECTOR vecIslandCenter = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	float fDistanceFromIslandCenter = common::Distance(vecPosition, vecIslandCenter);
	if (fDistanceFromIslandCenter > kfSpaceshipReturnDistance)
	{
		rFlags.Set(kReturnToIslandCenter);
	}
	else if (fDistanceFromIslandCenter < kfSpaceshipReturnedDistance)
	{
		rFlags.Clear(kReturnToIslandCenter);
	}

	XMVECTOR vecDestination = bPlayerAlive ? vecNearestPlayer : vecIslandCenter;
	if (rFlags & kReturnToIslandCenter)
	{
		vecDestination = vecIslandCenter;
	}
	// Trap: Normalize(0) produces NaN if ship is exactly at destination
	XMVECTOR vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(vecDestination, vecPosition));
	ASSERT(std::isfinite(XMVectorGetX(vecToDestinationNormal)) && std::isfinite(XMVectorGetY(vecToDestinationNormal)));
	float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(vecDirection, vecToDestinationNormal));
	float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfSpaceshipChaseTurnRate : -kfSpaceshipChaseTurnRate;
	if (!(rFlags & kReturnToIslandCenter) && rFlags & kFleePlayer)
	{
		fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? -kfSpaceshipFleeTurnRate : kfSpaceshipFleeTurnRate;
	}

	rfDeltaRotation = std::lerp(fWantedDeltaRotation, rfDeltaRotation, common::ExponentialDecay(kfSpaceshipSteeringSmoothing, fDeltaTime));
	rfDeltaRotation = common::ExponentialDecay(kfSpaceshipSteeringDecay, fDeltaTime) * rfDeltaRotation;

	// Trap: exit value used next tick to rotate direction
	ASSERT(std::isfinite(rfDeltaRotation));
}

void XM_CALLCONV SpaceshipsPostRender::ApplyMovement(Frame& __restrict rFrame, const SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, SpaceshipFlags_t flags, float fDeltaTime, XMVECTOR& rVecVelocity)
{
	// Trap: entry inputs. Direction must be unit-length and finite in all 4 lanes;
	// engine::ApplyMovement's internal Normalize propagates W-lane NaN into the returned velocity.
	ASSERT(std::isfinite(XMVectorGetX(rVecVelocity)) && std::isfinite(XMVectorGetY(rVecVelocity)) && std::isfinite(XMVectorGetZ(rVecVelocity)));
	const XMVECTOR vecShipDirection = rCurrentInterpolate.pVecDirections[i];
	ASSERT(std::isfinite(XMVectorGetX(vecShipDirection)) && std::isfinite(XMVectorGetY(vecShipDirection)) && std::isfinite(XMVectorGetZ(vecShipDirection)) && std::isfinite(XMVectorGetW(vecShipDirection)));

	float fAcceleration = flags & kReturnToIslandCenter ? kfSpaceshipReturnAcceleration
	                    : flags & kFleePlayer ? kfSpaceshipFleeAcceleration
	                    : kfSpaceshipChaseAcceleration;
	rVecVelocity = engine::ApplyMovement<true>(rVecVelocity, vecShipDirection, fDeltaTime, fAcceleration, kfSpaceshipDrag, kfSpaceshipMaxSpeed, kfSpaceshipVelocityToDirection);

	// Trap: isolates engine::ApplyMovement<true> (two Normalize hazards in FrameUtils.h)
	ASSERT(std::isfinite(XMVectorGetX(rVecVelocity)) && std::isfinite(XMVectorGetY(rVecVelocity)) && std::isfinite(XMVectorGetZ(rVecVelocity)));

	// Apply push from nearby pushers (pass own pusher ID to ignore self-push)
	XMVECTOR vecPush = engine::PushersInterpolate::ApplyPush(rFrame.interpolate, rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.puiPushers[i]);
	ASSERT(std::isfinite(XMVectorGetX(vecPush)) && std::isfinite(XMVectorGetY(vecPush)) && std::isfinite(XMVectorGetZ(vecPush)));

	float fPushLength = XMVectorGetX(XMVector3Length(vecPush));
	if (fPushLength > 0.0f)
	{
		XMVECTOR vecPushDirection = XMVectorDivide(vecPush, XMVectorReplicate(fPushLength));
		rVecVelocity = engine::ApplyClampedPush(rVecVelocity, vecPushDirection, fPushLength, kfSpaceshipMaxPusherPushVelocity);
		// Trap: isolates ApplyClampedPush
		ASSERT(std::isfinite(XMVectorGetX(rVecVelocity)) && std::isfinite(XMVectorGetY(rVecVelocity)) && std::isfinite(XMVectorGetZ(rVecVelocity)));
	}
}

void SpaceshipsPostRender::ApplyTerrainBounce(SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, float fDeltaTime, float& rfDeltaRotation, XMVECTOR& rVecVelocity)
{
	// Trap: spaceships must enter terrain bounce at BaseHeight
	ASSERT(XMVectorGetZ(rCurrentInterpolate.pVecPositions[i]) == engine::gBaseHeight.Get());

	float fTerrainElevation = engine::gpIslandTerrain->GlobalElevation(rCurrentInterpolate.pVecPositions[i]);
	if (fTerrainElevation >= XMVectorGetZ(rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
	{
		XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->GlobalNormal(rCurrentInterpolate.pVecPositions[i]), 0.0f));

		rCurrentInterpolate.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfSpaceshipTerrainBounceMove), vecTerrainNormal, rCurrentInterpolate.pVecPositions[i]);
		// Positions must always have W=1.0 — terrain normal has W=0 from XMVector3Cross, but
		// enforce the invariant defensively since this site bypasses the main integration clamp.
		rCurrentInterpolate.pVecPositions[i] = XMVectorSetW(rCurrentInterpolate.pVecPositions[i], 1.0f);

		float fDirectionTerrainCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecTerrainNormal));
		rfDeltaRotation = fDirectionTerrainCrossZ > 0.0f ? kfSpaceshipTerrainBounceRotation : -kfSpaceshipTerrainBounceRotation;

		rVecVelocity = XMVector3Reflect(rVecVelocity, vecTerrainNormal);
		rVecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfSpaceshipTerrainBounceVelocity), vecTerrainNormal, rVecVelocity);
	}

	// Trap: bounce output must preserve BaseHeight (catches zero-gradient normal → NaN Z leak)
	ASSERT(XMVectorGetZ(rCurrentInterpolate.pVecPositions[i]) == engine::gBaseHeight.Get());
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
			if (fDistanceToPlayer < kfSpaceshipAvoidTerrainPlayerDistance)
			{
				XMVECTOR vecToPlayerNormal = XMVector3Normalize(vecToPlayer);
				float fAngleToPlayer = XMVectorGetX(XMVector3AngleBetweenNormals(rCurrentInterpolate.pVecDirections[i], vecToPlayerNormal));
				if (fAngleToPlayer < kfSpaceshipAvoidTerrainPlayerAngle)
				{
					continue;
				}
			}
		}

		rCurrentInterpolate.pfDeltaRotations[i] = ComputeTerrainAvoidance(rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.pVecDirections[i], rCurrentInterpolate.pfDeltaRotations[i]);

		// Clamp delta rotation
		rCurrentInterpolate.pfDeltaRotations[i] = common::MinAbs(rCurrentInterpolate.pfDeltaRotations[i], kfSpaceshipMaxTurnRate);
	}
}

} // namespace game
