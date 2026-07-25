#include "Pch.h"

#include "Network/Server/FleetNavigationController.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/PlayerEvents.h"

namespace game
{

#if defined(BT_SERVER)

namespace
{

// Nav direction to coord offset mapping (0=+Y, 1=-Y, 2=+X, 3=-X)
engine::GridCoord NavDirectionOffset(int8_t iNavDirection)
{
	switch (iNavDirection)
	{
		case 0: return {0, 1};
		case 1: return {0, -1};
		case 2: return {1, 0};
		case 3: return {-1, 0};
		default: return {0, 0};
	}
}

} // namespace

void FleetNavigationController::TickFleetTimers(std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, common::RandomEngine& rRandom)
{
	for (auto& [rGuid, rFleetVec] : rFleets)
	{
		for (int64_t iFleet = 0; iFleet < std::ssize(rFleetVec); ++iFleet)
		{
			Fleet& rFleet = rFleetVec.at(static_cast<size_t>(iFleet));
			if (rFleet.iFlagshipIndex < 0 || rFleet.iFlagshipIndex >= std::ssize(rFleet.members))
			{
				continue;
			}

			const FleetMember& rFlagship = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex));
			if (!rFlagship.bAlive)
			{
				continue;
			}

			// Post-arrival countdown: the timer drains only while the flagship sits at the previously
			// picked wantedCoord. That makes fNavigationDelay an actual idle-at-destination delay
			// (cycle = transit + fNavigationDelay) rather than a wall-clock timer that overlaps with
			// transit and would let the next fire happen the instant the flagship arrives. Cardinal
			// mode is intentionally NOT gated here: if the flagship arrives mid-cardinal (heading
			// toward an edge), draining keeps going so that as soon as nav mode flips out of cardinal
			// the (likely already-negative) timer fires immediately — that's the un-freeze property.
			// Using mfLastDeltaTime (= iFullTicks * kfDeltaTime, set by GameBase::ServerUpdate after
			// the pause / time-scale resolution) keeps the timer in lockstep with frame-tick
			// progression. BuildFrameInputs calls this only for an advancing update, with the delta
			// scaled by mTimeStep under fast-forward / slow-mo.
			if (rFlagship.coord == rFleet.wantedCoord)
			{
				rFleet.fFrameChangeTimer -= gpGame->mfLastDeltaTime;
			}

			if (rFleet.fFrameChangeTimer > 0.0f)
			{
				continue;
			}

			// Fire preconditions: timer expiring is necessary but not sufficient — re-verify coord
			// match (drain implies coord==wantedCoord at last tick, but a cardinal-eject between
			// ticks could move the flagship), the cell is locally available, and the flagship isn't
			// already mid-cardinal toward an edge.
			if (!(rFlagship.coord == rFleet.wantedCoord))
			{
				continue;
			}
			if (!gpGame->mCoordFrames.contains(rFlagship.coord))
			{
				continue;
			}

			const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(rFlagship.coord).postRender.pPlayers;
			bool bFoundFlagship = false;
			int8_t iFlagshipNavDirection = -1;
			for (int64_t k = 0; k < rPlayers.iCount; ++k)
			{
				if (rPlayers.pGlobalPlayerIds[k] == rFlagship.globalPlayerId)
				{
					iFlagshipNavDirection = GetNavDirection(rPlayers.pFlags[k]);
					bFoundFlagship = true;
					break;
				}
			}
			if (!bFoundFlagship || (iFlagshipNavDirection >= 0 && iFlagshipNavDirection <= 3))
			{
				continue;
			}

			// Pick random cardinal direction and reset timer.
			int8_t iDirection = static_cast<int8_t>(common::Random(3u, rRandom));
			engine::GridCoord offset = NavDirectionOffset(iDirection);
			engine::GridCoord destination {rFlagship.coord.x + offset.x, rFlagship.coord.y + offset.y};
			uint8_t uiPendingTicks = static_cast<uint8_t>(engine::kiTickRate);
			rFleet.wantedCoord = destination;
			rFleet.uiPendingFleetWantedCoordTicks = uiPendingTicks;
			rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
			mPendingFlagshipUpdates.push_back({.clientGuid = rGuid, .fleetGuid = rFleet.guid, .newWantedCoord = destination, .uiPendingFleetWantedCoordTicks = uiPendingTicks});
			LOG(kNetwork, kVerbose, "FleetNavigationController::TickFleetTimers Guid: ({},{}) FleetGuid: ({},{}) Direction: {} WantedCoord: ({},{})", rGuid.uiHigh, rGuid.uiLow, rFleet.guid.uiHigh, rFleet.guid.uiLow, iDirection, destination.x, destination.y);
		}
	}
}

void FleetNavigationController::ProcessFlagshipUpdates(const std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets)
{
	for (const PendingFlagshipUpdate& rUpdate : mPendingFlagshipUpdates)
	{
		auto fleetIt = rFleets.find(rUpdate.clientGuid);
		if (fleetIt == rFleets.end())
		{
			continue;
		}

		auto matchIt = std::ranges::find(fleetIt->second, rUpdate.fleetGuid, &Fleet::guid);
		if (matchIt == fleetIt->second.end())
		{
			continue;
		}
		const Fleet& rFleet = *matchIt;

		if (rFleet.iFlagshipIndex >= std::ssize(rFleet.members))
		{
			continue;
		}

		// Send fleet wanted coord to all alive members
		int64_t iMembersUpdated = 0;
		for (int64_t i = 0; i < std::ssize(rFleet.members); ++i)
		{
			const FleetMember& rMember = rFleet.members.at(i);
			if (!rMember.bAlive)
			{
				continue;
			}

			engine::GridCoord memberCoord = rMember.coord;
			bool bMemberIsFlagship = (i == rFleet.iFlagshipIndex);

			auto frameInputIt = gpGame->mFrameInputs.find(memberCoord);
			if (frameInputIt == gpGame->mFrameInputs.end())
			{
				continue;
			}
			if (!gpGame->mCoordFrames.contains(memberCoord))
			{
				continue;
			}

			const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(memberCoord).postRender.pPlayers;
			for (int64_t k = 0; k < rPlayers.iCount; ++k)
			{
				if (rPlayers.pGlobalPlayerIds[k] == rMember.globalPlayerId)
				{
					int64_t iPlayerUuid = rPlayers.puiIds[k].ToUuid().Value();
					frameInputIt->second.statusChanges.push_back({
						.eType = StatusChangeType::kUpdateFleet,
						.data = UpdateFleetData {
							.iPlayerUuid = iPlayerUuid,
							.bIsFlagship = bMemberIsFlagship,
							.fleetWantedCoord = rUpdate.newWantedCoord,
							.uiPendingFleetWantedCoordTicks = rUpdate.uiPendingFleetWantedCoordTicks,
						},
					});
					++iMembersUpdated;
					break;
				}
			}
		}
		LOG(kNetwork, kVerbose, "FleetNavigationController::ProcessFlagshipUpdates Guid: ({},{}) FleetGuid: ({},{}) MembersUpdated: {} WantedCoord: ({},{})", rUpdate.clientGuid.uiHigh, rUpdate.clientGuid.uiLow, rUpdate.fleetGuid.uiHigh, rUpdate.fleetGuid.uiLow, iMembersUpdated, rUpdate.newWantedCoord.x, rUpdate.newWantedCoord.y);
	}
	mPendingFlagshipUpdates.clear();
}

void FleetNavigationController::QueueFlagshipUpdate(const PendingFlagshipUpdate& rUpdate)
{
	mPendingFlagshipUpdates.push_back(rUpdate);
}

void FleetNavigationController::ClearPendingFlagshipUpdates()
{
	mPendingFlagshipUpdates.clear();
}

void FleetNavigationController::ShiftFlagshipAfterDeath(const engine::ClientGuid& rGuid, Fleet& rFleet)
{
	int64_t iNewFlagship = -1;
	for (int64_t k = 1; k < std::ssize(rFleet.members); ++k)
	{
		int64_t iCandidate = (rFleet.iFlagshipIndex + k) % std::ssize(rFleet.members);
		if (rFleet.members.at(static_cast<size_t>(iCandidate)).bAlive)
		{
			iNewFlagship = iCandidate;
			break;
		}
	}
	if (iNewFlagship < 0)
	{
		return;
	}

	rFleet.iFlagshipIndex = iNewFlagship;
	rFleet.wantedCoord = rFleet.members.at(static_cast<size_t>(iNewFlagship)).coord;
	rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
	mPendingFlagshipUpdates.push_back({.clientGuid = rGuid, .fleetGuid = rFleet.guid, .newWantedCoord = rFleet.wantedCoord});
}

#endif // BT_SERVER

} // namespace game
