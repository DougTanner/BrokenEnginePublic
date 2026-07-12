#include "Spaceships.h"

#include "Frame/FrameStaticData.h"
#include "Frame/TerrainUtils.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Pushers/Pushers.h"

namespace game
{

using enum SpaceshipFlags;

// Spaceship AI — acceleration per behavior
constexpr float kfSpaceshipChaseAcceleration = 4.0f;
constexpr float kfSpaceshipFleeAcceleration = 6.0f;
constexpr float kfSpaceshipReturnAcceleration = kfSpaceshipAcceleration;

// Steering
constexpr float kfSpaceshipSteeringSmoothing = 0.064f;
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

// Terrain avoidance (player-proximity skip constants; sampling constants in TerrainUtils.cpp)
constexpr float kfSpaceshipAvoidTerrainPlayerAngle = 0.4f;
constexpr float kfSpaceshipAvoidTerrainPlayerDistance = 40.0f;

// Forward declarations for shared helpers (defined in Spaceships.cpp)
[[nodiscard]] bool XM_CALLCONV NearestAlivePlayerPosition(const PlayersInterpolate& rPlayers, const PlayersPostRender& rPlayersPostRender, FXMVECTOR vecFrom, XMVECTOR& rVecResult);

void XM_CALLCONV SpaceshipsPostRender::ComputeSteering(std::span<const XMFLOAT4> islandCandidates, FXMVECTOR vecPosition, FXMVECTOR vecDirection, bool bPlayerAlive, FXMVECTOR vecNearestPlayer, float fDeltaTime, SpaceshipFlags_t& rFlags, float& rfDeltaRotation)
{
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

	// Per-cell island center: pick the nearest placement to this spaceship for return-to-island steering.
	// Candidates are precomputed once per coord in Update (tick-invariant across ships); values are bit-identical
	// to the former per-ship XMVectorSet(x, y, gBaseHeight.Get(), 1.0f) — same floats, computed once.
	XMVECTOR vecIslandCenter = XMLoadFloat4(&islandCandidates[0]);
	float fNearestDistanceSq = std::numeric_limits<float>::max();
	for (const XMFLOAT4& rCandidate : islandCandidates)
	{
		XMVECTOR vecCandidate = XMLoadFloat4(&rCandidate);
		float fDistSq = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecCandidate, vecPosition)));
		if (fDistSq < fNearestDistanceSq)
		{
			fNearestDistanceSq = fDistSq;
			vecIslandCenter = vecCandidate;
		}
	}
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
	XMVECTOR vecToDestinationNormal = XMVector3Normalize(XMVectorSubtract(vecDestination, vecPosition));
	float fDirectionDestinationCrossZ = XMVectorGetZ(XMVector3Cross(vecDirection, vecToDestinationNormal));
	float fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? kfSpaceshipChaseTurnRate : -kfSpaceshipChaseTurnRate;
	if (!(rFlags & kReturnToIslandCenter) && rFlags & kFleePlayer)
	{
		fWantedDeltaRotation = fDirectionDestinationCrossZ > 0.0f ? -kfSpaceshipFleeTurnRate : kfSpaceshipFleeTurnRate;
	}

	rfDeltaRotation = std::lerp(fWantedDeltaRotation, rfDeltaRotation, common::ExponentialDecay(kfSpaceshipSteeringSmoothing, fDeltaTime));
	rfDeltaRotation = common::ExponentialDecay(kfSpaceshipSteeringDecay, fDeltaTime) * rfDeltaRotation;
}

void XM_CALLCONV SpaceshipsPostRender::ApplyMovement(Frame& __restrict rFrame, const SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, SpaceshipFlags_t flags, float fDeltaTime, XMVECTOR& rVecVelocity)
{
	float fAcceleration = flags & kReturnToIslandCenter ? kfSpaceshipReturnAcceleration
	                    : flags & kFleePlayer ? kfSpaceshipFleeAcceleration
	                    : kfSpaceshipChaseAcceleration;
	rVecVelocity = engine::ApplyMovement<true>(rVecVelocity, rCurrentInterpolate.pVecDirections[i], fDeltaTime, fAcceleration, kfSpaceshipDrag, kfSpaceshipMaxSpeed, kfSpaceshipVelocityToDirection);

	// Apply push from nearby pushers (pass own pusher ID to ignore self-push)
	XMVECTOR vecPush = engine::PushersInterpolate::ApplyPush(rFrame.interpolate, rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.puiPushers[i]);

	float fPushLength = XMVectorGetX(XMVector3Length(vecPush));
	if (fPushLength > 0.0f)
	{
		XMVECTOR vecPushDirection = XMVectorDivide(vecPush, XMVectorReplicate(fPushLength));
		rVecVelocity = engine::ApplyClampedPush(rVecVelocity, vecPushDirection, fPushLength, kfSpaceshipMaxPusherPushVelocity);
	}
}

void SpaceshipsPostRender::ApplyTerrainBounce(const engine::FrameStaticData& rStaticData, SpaceshipsInterpolate& __restrict rCurrentInterpolate, int64_t i, float fDeltaTime, float& rfDeltaRotation, XMVECTOR& rVecVelocity)
{
	float fTerrainElevation = engine::gpIslandTerrain->FrameElevation(rStaticData, rCurrentInterpolate.pVecPositions[i]);
	if (fTerrainElevation >= XMVectorGetZ(rCurrentInterpolate.pVecPositions[i])) [[unlikely]]
	{
		XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->FrameNormal(rStaticData, rCurrentInterpolate.pVecPositions[i]), 0.0f));

		rCurrentInterpolate.pVecPositions[i] = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfSpaceshipTerrainBounceMove), vecTerrainNormal, rCurrentInterpolate.pVecPositions[i]);
		// Enforce W=1.0 — terrain-bounce bypasses the main integration clamp.
		rCurrentInterpolate.pVecPositions[i] = XMVectorSetW(rCurrentInterpolate.pVecPositions[i], 1.0f);

		float fDirectionTerrainCrossZ = XMVectorGetZ(XMVector3Cross(rCurrentInterpolate.pVecDirections[i], vecTerrainNormal));
		rfDeltaRotation = fDirectionTerrainCrossZ > 0.0f ? kfSpaceshipTerrainBounceRotation : -kfSpaceshipTerrainBounceRotation;

		rVecVelocity = XMVector3Reflect(rVecVelocity, vecTerrainNormal);
		rVecVelocity = XMVectorMultiplyAdd(XMVectorReplicate(fDeltaTime * kfSpaceshipTerrainBounceVelocity), vecTerrainNormal, rVecVelocity);
	}
}

void SpaceshipsPostRender::AvoidTerrain([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, const engine::FrameStaticData& rStaticData, int64_t iStart, int64_t iEnd)
{
	SpaceshipsInterpolate& rCurrentInterpolate = *rFrame.interpolate.pSpaceships;
	SpaceshipsPostRender& rCurrentPostRender = *rFrame.postRender.pSpaceships;

	for (int64_t i = iStart; i < iEnd; ++i)
	{
		if (rCurrentPostRender.pFlags[i] & kExploding) [[unlikely]]
		{
			continue;
		}

		// Skip terrain avoidance if close to nearest alive player and facing them. Reads current-frame players
		// (race-free: Players update before Spaceships this tick). Deliberately differs from
		// SpaceshipsPostRender::Update, which scans previous-frame players — see the rationale comment there.
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

		rCurrentInterpolate.pfDeltaRotations[i] = ComputeTerrainAvoidance(rStaticData, rCurrentInterpolate.pVecPositions[i], rCurrentInterpolate.pVecDirections[i], rCurrentInterpolate.pfDeltaRotations[i]);

		// Clamp delta rotation
		rCurrentInterpolate.pfDeltaRotations[i] = common::MinAbs(rCurrentInterpolate.pfDeltaRotations[i], kfSpaceshipMaxTurnRate);
	}
}

} // namespace game
