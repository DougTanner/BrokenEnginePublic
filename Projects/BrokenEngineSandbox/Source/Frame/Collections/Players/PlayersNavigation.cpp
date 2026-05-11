#include "Players.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandTerrain.h"
#include "Frame/NavQuery.h"
#include "Frame/TerrainUtils.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Pushers/Pushers.h"

namespace game
{

using enum PlayerFlags;

// Terrain collision
constexpr float kfTerrainPushVelocity = 15.0f;
constexpr float kfMaxPushVelocity = 20.0f;

// Flagship follow
constexpr float kfFlagshipFollowDistanceSquared = 50.0f * 50.0f;
constexpr float kfFlagshipCloseDistanceSquared = 12.5f * 12.5f;

void PlayersPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
	PlayersInterpolate& rCurrentInterpolate = *rFrame.interpolate.pPlayers;
	PlayersPostRender& rCurrentPostRender = *rFrame.postRender.pPlayers;

	const FrameBounds bounds = ComputeFrameBounds(rStaticData.vecArea);

	// Reverse iteration for swap-and-pop safety with RemoveIndexableElement
	for (int64_t i = rCurrentInterpolate.iCount - 1; i >= 0; --i)
	{
		if (!(rCurrentPostRender.pFlags[i] & kTransfer)) [[likely]]
		{
			continue;
		}

		XMVECTOR vecPosition = rCurrentInterpolate.pVecPositions[i];

		// Build transfer request
		TransferRequest request
		{
			.eType = StatusChangeType::kTransferPlayer,
			.data = {
				.vecPosition = vecPosition,
				.vecDirection = rCurrentInterpolate.pVecDirections[i],
				.vecVelocity = rCurrentPostRender.pVecVelocities[i],
				.alignment = rCurrentPostRender.pAlignments[i],
				.fHealth = rCurrentPostRender.pfArmors[i],
				.fShield = rCurrentPostRender.pfShields[i],
				.fNextBlasterFireTime = rCurrentPostRender.pfNextBlasterFireTimes[i],
				.fNextSecondarySpawnTime = rCurrentPostRender.pfNextSecondarySpawnTimes[i],
				.fShieldCooldown = rCurrentPostRender.pfShieldCooldowns[i],
				.fShieldDownSoundCooldown = rCurrentPostRender.pfShieldDownSoundCooldowns[i],
				.fAnimationTime = rCurrentInterpolate.pfAnimationTimes[i],
#if defined(BT_CLIENT)
				.fShieldRotation = rCurrentInterpolate.pfShieldRotations[i],
				.fShieldShrink = rCurrentInterpolate.pfShieldShrinks[i],
#endif
				.uiPlayerFlags = static_cast<uint16_t>(std::to_underlying(rCurrentPostRender.pFlags[i].meFlags) & ~std::to_underlying(kTransfer)),
				.fArrivalGracePeriod = rCurrentPostRender.pfArrivalGracePeriods[i],
				.fNavigationDelay = rCurrentPostRender.pfNavigationDelays[i],
			},
			.iEntityId = rCurrentPostRender.puiIds[i].ToUuid().Value(),
			.iPushedTick = rFrame.interpolate.iTick,
		};
		request.data.globalPlayerId = rCurrentPostRender.pGlobalPlayerIds[i];
		request.data.fleetWantedCoord = rCurrentPostRender.pFleetWantedCoords[i];
		request.data.uiPendingFleetWantedCoordTicks = rCurrentPostRender.puiPendingFleetWantedCoordTicks[i];
		request.data.uiPendingWeaponModeTicks = rCurrentPostRender.puiPendingWeaponModeTicks[i];
		request.data.uiClientGuidHigh = rCurrentPostRender.pClientGuids[i].uiHigh;
		request.data.uiClientGuidLow = rCurrentPostRender.pClientGuids[i].uiLow;
		ComputeTransferDelta(bounds, vecPosition, request.iDeltaX, request.iDeltaY);

		// Heap realloc warning: capacity exceeded during burst transfers. Expected max ~1-2/tick
		// per source frame — anything higher suggests entities are re-flagging kTransfer across
		// iterations, DestroyElement isn't removing them, or there's an unexpected push path.
		if (rFrame.postRender.transferRequests.size() == rFrame.postRender.transferRequests.capacity()) [[unlikely]]
		{
			LOG(kDefault, kError,
				"Player Transfer capacity hit Tick: {} Source: ({},{}) Index: {} Position: {} Velocity: {} Delta: ({},{}) GlobalPlayerId: {} Alignment: {} SourceCount: {} Pushed: {} Capacity: {}",
				rFrame.interpolate.iTick,
				rStaticData.coord.x, rStaticData.coord.y,
				i,
				common::WbV2(vecPosition, 1),
				common::WbV2(rCurrentPostRender.pVecVelocities[i], 1),
				static_cast<int32_t>(request.iDeltaX), static_cast<int32_t>(request.iDeltaY),
				rCurrentPostRender.pGlobalPlayerIds[i],
				rCurrentPostRender.pAlignments[i],
				rCurrentInterpolate.iCount,
				rFrame.postRender.transferRequests.size(),
				rFrame.postRender.transferRequests.capacity());
			DEBUG_BREAK();
		}
		common::ValidateVector<true >(request.data.vecPosition);
		common::ValidateVector<false>(request.data.vecDirection);
		common::ValidateVector<false>(request.data.vecVelocity);
		rFrame.postRender.transferRequests.push_back(request);

		engine::PushersPostRender::Remove(rFrame, rCurrentInterpolate.puiPushers[i]);
#if defined(BT_CLIENT)
		PlayersInterpolate::RemoveOwnedVisuals(rFrame, rCurrentInterpolate, i);
#endif // BT_CLIENT

		engine::RemoveIndexableElement(rCurrentInterpolate, rCurrentPostRender, rCurrentPostRender.puiIds[i], rCurrentInterpolate.Members(), rCurrentPostRender.Members());
	}
}

void XM_CALLCONV PlayersPostRender::ComputeNavigation([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData, int64_t i, FXMVECTOR vecPosition, FXMVECTOR vecFrameCenter, engine::GridCoord fleetWantedCoord, uint8_t uiPendingFleetWantedCoordTicks, PlayerFlags_t flags, float fNavigationDelay, float fDeltaTime, int8_t& riNavDirection, XMVECTOR& rVecAiDirection, XMVECTOR& rVecIslandDestination, float& rfFrameChangeTimer)
{
	PlayersPostRender& __restrict rCurrent = *rFrame.postRender.pPlayers;
	const PlayersPostRender& rPrevious = *rPreviousFrame.postRender.pPlayers;
	const PlayersInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pPlayers;
	XMVECTOR vecArea = rStaticData.vecArea;

	// Initialize direction if zero (first spawn or after reset)
	if (XMVectorGetX(XMVector3LengthSq(rVecAiDirection)) < 0.001f)
	{
		float fAngle = common::Random<XM_2PI>(rFrame.postRender.randomEngine);
		rVecAiDirection = XMVector4Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngle));
	}

	// Fleet navigation: navigate toward fleet's wanted coord after countdown expires
	if (!(fleetWantedCoord == rStaticData.coord) && uiPendingFleetWantedCoordTicks == 0)
	{
		int32_t iDeltaX = fleetWantedCoord.x - rStaticData.coord.x;
		int32_t iDeltaY = fleetWantedCoord.y - rStaticData.coord.y;

		// Check if already heading in a valid direction toward wanted coord
		bool bAlreadyValid = false;
		if (riNavDirection >= 0 && riNavDirection <= 3)
		{
			switch (riNavDirection)
			{
				case 0: bAlreadyValid = iDeltaY > 0; break;
				case 1: bAlreadyValid = iDeltaY < 0; break;
				case 2: bAlreadyValid = iDeltaX > 0; break;
				case 3: bAlreadyValid = iDeltaX < 0; break;
				default: break;
			}
		}

		if (!bAlreadyValid)
		{
			int8_t iRandom = static_cast<int8_t>(common::Random(3u, rFrame.postRender.randomEngine));
			if (iDeltaX != 0 && iDeltaY != 0)
			{
				riNavDirection = (iRandom < 2)
					? (iDeltaY > 0 ? 0 : 1)
					: (iDeltaX > 0 ? 2 : 3);
			}
			else if (iDeltaY != 0)
			{
				riNavDirection = iDeltaY > 0 ? 0 : 1;
			}
			else
			{
				riNavDirection = iDeltaX > 0 ? 2 : 3;
			}
			rVecIslandDestination = XMVectorZero();
			LOG(kNavData, kVerbose, "Player {} GlobalId: {} fleet override NavDir: {} WantedCoord: ({},{}) CellCoord: ({},{})", i, rCurrent.pGlobalPlayerIds[i], riNavDirection, fleetWantedCoord.x, fleetWantedCoord.y, rStaticData.coord.x, rStaticData.coord.y);
		}
	}

	// Flagship proximity: non-flagship in same cell follows flagship
	if (!(flags & kIsFlagship) && (fleetWantedCoord == rStaticData.coord))
	{
		for (int64_t j = 0; j < rPrevious.iCount; ++j)
		{
			if (j == i)
			{
				continue;
			}
			if (!(rPrevious.pFlags[j] & kIsFlagship))
			{
				continue;
			}

			XMVECTOR vecFlagshipPos = rPreviousInterpolate.pVecPositions[j];
			float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecPosition, vecFlagshipPos)));

			if (fDistanceSquared > kfFlagshipFollowDistanceSquared)
			{
				riNavDirection = 5;
				rVecIslandDestination = XMVectorSetW(vecFlagshipPos, 1.0f);
			}
			else if (riNavDirection == 5)
			{
				rVecIslandDestination = XMVectorSetW(vecFlagshipPos, 1.0f);
				if (fDistanceSquared < kfFlagshipCloseDistanceSquared)
				{
					riNavDirection = 4;
					rVecIslandDestination = XMVectorZero();
				}
			}
			break;
		}
	}

	// Frame change timer: cycle back to island destination when roaming
	if (riNavDirection == -1)
	{
		rfFrameChangeTimer -= fDeltaTime;
		if (rfFrameChangeTimer <= 0.0f)
		{
			riNavDirection = 4;
		}
	}

	if (riNavDirection == 5)
	{
		// Following flagship via NavQuery pathfinding
		// Consume randoms for determinism (mode 4 would consume these for destination generation)
		common::Random<Frame::kfIslandWidth>(rFrame.postRender.randomEngine);
		common::Random<Frame::kfIslandHeight>(rFrame.postRender.randomEngine);

		XMVECTOR vecDebugWaypoint = XMVectorZero();
		XMVECTOR vecNavDirection = XMVectorZero();
		{
			engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateNavQuery);
			vecNavDirection = engine::NavQueryDirection(vecPosition, rVecIslandDestination, rStaticData.navData, &vecDebugWaypoint);
		}
		if (XMVectorGetX(XMVector3LengthSq(vecNavDirection)) > 0.001f)
		{
			rVecAiDirection = vecNavDirection;
		}
		else
		{
			rVecAiDirection = XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(rVecIslandDestination, vecPosition), 0.0f));
		}
#if defined(BT_CLIENT)
		if constexpr (kbDebugRender)
		{
			rCurrent.pVecDebugNavWaypoints[i] = vecDebugWaypoint;
		}
#endif // BT_CLIENT
	}
	else if (riNavDirection == 4)
	{
		// Navigate to random island destination
		if (XMVectorGetW(rVecIslandDestination) == 0.0f)
		{
			// Pick a placement deterministically by player index; generate a random point inside
			// its rotated AABB. RNG state advances by exactly 2 per Players/CLAUDE.md mode-5 invariant.
			const engine::IslandPlacement& rPlacement = rStaticData.islands.at(static_cast<size_t>(i) % rStaticData.islands.size());
			const engine::IslandTemplate& rTemplate = engine::gpIslandTerrain->mIslands.at(rPlacement.islandCrc);
			float fIslandMinX = rPlacement.f2WorldPos.x - 0.5f * rTemplate.mfQuadWidth;
			float fIslandMinY = rPlacement.f2WorldPos.y - 0.5f * rTemplate.mfQuadHeight;

			float fX = fIslandMinX + common::Random(rTemplate.mfQuadWidth, rFrame.postRender.randomEngine);
			float fY = fIslandMinY + common::Random(rTemplate.mfQuadHeight, rFrame.postRender.randomEngine);
			rVecIslandDestination = XMVectorSet(fX, fY, engine::gBaseHeight.Get(), 1.0f);

			// Snap to navigable area if inside an obstacle
			rVecIslandDestination = engine::NavQuerySnapToNavigable(rVecIslandDestination, rStaticData.navData);
			LOG(kNavData, kVerbose, "Player {} NavSwitch: island dest generated pos={} dest={}", i, vecPosition, rVecIslandDestination);
		}

		XMVECTOR vecDebugWaypoint = XMVectorZero();
		XMVECTOR vecNavDirection = XMVectorZero();
		{
			engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateNavQuery);
			vecNavDirection = engine::NavQueryDirection(vecPosition, rVecIslandDestination, rStaticData.navData, &vecDebugWaypoint);
		}
		if (XMVectorGetX(XMVector3LengthSq(vecNavDirection)) > 0.001f)
		{
			rVecAiDirection = vecNavDirection;
		}
		else
		{
			rVecAiDirection = XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(rVecIslandDestination, vecPosition), 0.0f));
		}

		// Arrival check
		float fDistanceSquared = XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(vecPosition, rVecIslandDestination)));
		if (fDistanceSquared < 100.0f)
		{
			LOG(kNavData, kVerbose, "Player {} NavSwitch: arrived at island dest pos={} dest={}", i, vecPosition, rVecIslandDestination);
			riNavDirection = -1;
			rfFrameChangeTimer = fNavigationDelay;
			rVecIslandDestination = XMVectorZero();
		}
#if defined(BT_CLIENT)
		if constexpr (kbDebugRender)
		{
			rCurrent.pVecDebugNavWaypoints[i] = vecDebugWaypoint;
		}
#endif // BT_CLIENT
	}
	else if (riNavDirection >= 0)
	{
		// Navigate toward neighboring frame center
		rVecIslandDestination = XMVectorZero();
		XMVECTOR vecDestination = vecFrameCenter;
		switch (riNavDirection)
		{
			case 0:
				vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(0.0f, Frame::kfCellHeight, 0.0f, 0.0f));
				break;
			case 1:
				vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(0.0f, -Frame::kfCellHeight, 0.0f, 0.0f));
				break;
			case 2:
				vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(Frame::kfCellWidth, 0.0f, 0.0f, 0.0f));
				break;
			case 3:
				vecDestination = XMVectorAdd(vecFrameCenter, XMVectorSet(-Frame::kfCellWidth, 0.0f, 0.0f, 0.0f));
				break;
			default:
				break;
		}

		XMVECTOR vecDebugWaypoint = XMVectorZero();
		XMVECTOR vecNavDirection = XMVectorZero();
		{
			engine::ScopedCpuProfile scopedCpuProfile(game::kCpuTimerPostRenderUpdateNavQuery);
			vecNavDirection = engine::NavQueryDirection(vecPosition, vecDestination, rStaticData.navData, kbDebugRender ? &vecDebugWaypoint : nullptr);
		}
		float fNavLengthSquared = XMVectorGetX(XMVector3LengthSq(vecNavDirection));
		if (fNavLengthSquared > 0.001f)
		{
			rVecAiDirection = vecNavDirection;
		}
		else
		{
			rVecAiDirection = XMVector3Normalize(XMVectorSetZ(XMVectorSubtract(vecDestination, vecPosition), 0.0f));
			LOG(kNavData, kWarning, "Player {} navQuery returned zero, fallback dir={}", i, rVecAiDirection);
		}
#if defined(BT_CLIENT)
		if constexpr (kbDebugRender)
		{
			rCurrent.pVecDebugNavWaypoints[i] = vecDebugWaypoint;
		}
#endif // BT_CLIENT
	}
	else
	{
		rVecIslandDestination = XMVectorZero();
		auto [vecNewAiDirection] = ComputeAiSteering(vecPosition, rVecAiDirection, vecFrameCenter, fDeltaTime, i % 2 == 0);
		rVecAiDirection = vecNewAiDirection;
#if defined(BT_CLIENT)
		if constexpr (kbDebugRender)
		{
			rCurrent.pVecDebugNavWaypoints[i] = XMVectorZero();
		}
#endif // BT_CLIENT
	}
}

void XM_CALLCONV PlayersPostRender::ApplyMovement(int8_t iNavDirection, FXMVECTOR vecAiDirection, float fDeltaTime, float fAccelMul, float fDecayMul, XMVECTOR& rVecVelocity)
{
	float fAcceleration = (iNavDirection == 5 ? kfPlayerCatchUpAcceleration : kfPlayerAcceleration) * fAccelMul;
	float fMaxSpeed = iNavDirection == 5 ? kfPlayerCatchUpMaxSpeed : kfPlayerMaxSpeed;
	rVecVelocity = engine::ApplyMovement(rVecVelocity, vecAiDirection, fDeltaTime, fAcceleration, kfPlayerDrag * fDecayMul, fMaxSpeed);
}

void XM_CALLCONV PlayersPostRender::ApplyTerrainPush(FXMVECTOR vecPosition, XMVECTOR& rVecVelocity)
{
	// Terrain collision - add velocity away from terrain, gentle at first then ramping up
	float fElevation = engine::gpIslandTerrain->GlobalElevation(vecPosition);
	float fPushHeight = engine::gBaseHeight.Get() - kfPlayerRadius - kfPushMargin;
	if (fElevation >= fPushHeight) [[unlikely]]
	{
		XMVECTOR vecTerrainNormal = XMVector3Normalize(XMVectorSetZ(engine::gpIslandTerrain->GlobalNormal(vecPosition), 0.0f));
		float fPenetration = fElevation - fPushHeight;
		float fPushStrength = fPenetration * fPenetration * kfTerrainPushVelocity;
		rVecVelocity = engine::ApplyClampedPush(rVecVelocity, vecTerrainNormal, fPushStrength, kfMaxPushVelocity);
	}
}

void XM_CALLCONV PlayersPostRender::ApplyPusherPush(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, int64_t i, FXMVECTOR vecPosition, XMVECTOR& rVecVelocity)
{
	const PlayersInterpolate& rPreviousInterpolate = *rPreviousFrame.interpolate.pPlayers;

	// Player-to-player push (prevents overlap)
	XMVECTOR vecPush = engine::PushersInterpolate::ApplyPush(rFrame.interpolate, vecPosition, rPreviousInterpolate.puiPushers[i]);
	float fPushLength = XMVectorGetX(XMVector3Length(vecPush));
	if (fPushLength > 0.0f)
	{
		XMVECTOR vecPushDirection = XMVectorDivide(vecPush, XMVectorReplicate(fPushLength));
		rVecVelocity = engine::ApplyClampedPush(rVecVelocity, vecPushDirection, fPushLength, kfPlayerMaxPusherPushVelocity);
	}
}

} // namespace game
