#include "Pch.h"

#include "Network/Server/ServerFleetManager.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/GamePacketType.h"
#include "Network/PlayerEvents.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerSession.h"

namespace game
{

#if defined(BT_SERVER)

ServerFleetManager::ServerFleetManager()
{
	mRandomEngine.TimeSeed();
}

engine::ClientGuid ServerFleetManager::FindGuidForClient(int64_t iClientId) const
{
	for (const auto& [rGuid, iId] : mGuidToClientId)
	{
		if (iId == iClientId)
		{
			return rGuid;
		}
	}
	return {};
}

int64_t ServerFleetManager::FindClientIdForGuid(const engine::ClientGuid& rGuid) const
{
	auto it = mGuidToClientId.find(rGuid);
	if (it != mGuidToClientId.end())
	{
		return it->second;
	}
	return 0;
}

void ServerFleetManager::ProcessCreateFleetRequests()
{
	// Heap: mFleets map and fleet-list grow on create; SendFleetSyncToClient allocates packet
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingCreateFleetRequest& rRequest : mPendingCreateFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		Fleet& rNewFleet = mFleets.try_emplace(guid).first->second.emplace_back();
		rNewFleet.guid.uiHigh = common::RandomNext(mRandomEngine);
		rNewFleet.guid.uiLow = common::RandomNext(mRandomEngine);
		mGuidToClientId.insert_or_assign(guid, rRequest.iClientId);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessCreateFleetRequests Client: {} FleetCount: {} FleetGuid: ({},{})", rRequest.iClientId, mFleets.at(guid).size(), rNewFleet.guid.uiHigh, rNewFleet.guid.uiLow);
		SendFleetSyncToClient(rRequest.iClientId);
	}
}

void ServerFleetManager::ProcessDeleteFleetRequests()
{
	// Heap: SendFleetSyncToClient allocates packet on fleet delete
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingDeleteFleetRequest& rRequest : mPendingDeleteFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		auto it = mFleets.find(guid);
		if (it == mFleets.end() || rRequest.iFleetIndex < 0 || rRequest.iFleetIndex >= std::ssize(it->second))
		{
			continue;
		}

		Fleet& rFleet = it->second.at(static_cast<size_t>(rRequest.iFleetIndex));
		if (!rFleet.members.empty())
		{
			continue;
		}

		it->second.erase(it->second.begin() + rRequest.iFleetIndex);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessDeleteFleetRequests Client: {} Fleet: {} FleetCount: {}", rRequest.iClientId, rRequest.iFleetIndex, it->second.size());
		SendFleetSyncToClient(rRequest.iClientId);
	}
}

void ServerFleetManager::ProcessSpawnIntoFleetRequests()
{
	// Heap: QueueSpawnForClient appends to spawn queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingSpawnIntoFleetRequest& rRequest : mPendingSpawnIntoFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		auto it = mFleets.find(guid);
		if (it == mFleets.end() || rRequest.iFleetIndex < 0 || rRequest.iFleetIndex >= std::ssize(it->second))
		{
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, engine::kOriginCoord, rRequest.iFleetIndex, -1);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessSpawnIntoFleetRequests Client: {} Fleet: {}", rRequest.iClientId, rRequest.iFleetIndex);
	}
}

void ServerFleetManager::ProcessRespawnInFleetRequests()
{
	// Heap: QueueSpawnForClient appends to spawn queue on respawn
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingRespawnInFleetRequest& rRequest : mPendingRespawnInFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		auto it = mFleets.find(guid);
		if (it == mFleets.end() || rRequest.iFleetIndex < 0 || rRequest.iFleetIndex >= std::ssize(it->second))
		{
			continue;
		}

		const Fleet& rFleet = it->second.at(static_cast<size_t>(rRequest.iFleetIndex));
		if (rRequest.iMemberIndex < 0 || rRequest.iMemberIndex >= std::ssize(rFleet.members))
		{
			continue;
		}

		if (rFleet.members.at(static_cast<size_t>(rRequest.iMemberIndex)).bAlive)
		{
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, engine::kOriginCoord, rRequest.iFleetIndex, rRequest.iMemberIndex);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessRespawnInFleetRequests Client: {} Fleet: {} Member: {}", rRequest.iClientId, rRequest.iFleetIndex, rRequest.iMemberIndex);
	}
}

// Nav direction to coord offset mapping (0=+Y, 1=-Y, 2=+X, 3=-X)
static engine::GridCoord NavDirectionOffset(int8_t iNavDirection)
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

void ServerFleetManager::TickFleetTimers()
{
	// Heap: mPendingFlagshipUpdates vector grows when flagship picks new direction
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (auto& [rGuid, rFleets] : mFleets)
	{
		for (int64_t iFleet = 0; iFleet < std::ssize(rFleets); ++iFleet)
		{
			Fleet& rFleet = rFleets.at(static_cast<size_t>(iFleet));
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
			// progression: zero during pause, scaled by mTimeStep under fast-forward / slow-mo.
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
			int8_t iDirection = static_cast<int8_t>(common::Random(3u, mRandomEngine));
			engine::GridCoord offset = NavDirectionOffset(iDirection);
			engine::GridCoord destination {rFlagship.coord.x + offset.x, rFlagship.coord.y + offset.y};
			uint8_t uiPendingTicks = static_cast<uint8_t>(engine::kiTickRate);
			rFleet.wantedCoord = destination;
			rFleet.uiPendingFleetWantedCoordTicks = uiPendingTicks;
			rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
			mPendingFlagshipUpdates.push_back({rGuid, iFleet, destination, uiPendingTicks});
			LOG(kNetwork, kVerbose, "ServerFleetManager::TickFleetTimers Guid: ({},{}) Fleet: {} Direction: {} WantedCoord: ({},{})", rGuid.uiHigh, rGuid.uiLow, iFleet, iDirection, destination.x, destination.y);
		}
	}
}

void ServerFleetManager::ProcessFlagshipUpdates()
{
	// Heap: per-member statusChanges vector grows on flagship update broadcast
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingFlagshipUpdate& rUpdate : mPendingFlagshipUpdates)
	{
		auto fleetIt = mFleets.find(rUpdate.clientGuid);
		if (fleetIt == mFleets.end())
		{
			continue;
		}

		if (rUpdate.iFleetIndex >= std::ssize(fleetIt->second))
		{
			continue;
		}
		const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(rUpdate.iFleetIndex));

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
		LOG(kNetwork, kVerbose, "ServerFleetManager::ProcessFlagshipUpdates Guid: ({},{}) Fleet: {} MembersUpdated: {} WantedCoord: ({},{})",
			rUpdate.clientGuid.uiHigh, rUpdate.clientGuid.uiLow, rUpdate.iFleetIndex, iMembersUpdated, rUpdate.newWantedCoord.x, rUpdate.newWantedCoord.y);
	}
	mPendingFlagshipUpdates.clear();
}

void ServerFleetManager::SendFleetSyncToClient(int64_t iClientId)
{
	engine::ClientGuid guid = FindGuidForClient(iClientId);
	auto it = mFleets.find(guid);
	if (it != mFleets.end())
	{
		SendFleetSync(iClientId, it->second);
	}
	else
	{
		SendFleetSync(iClientId, {});
	}
}

void ServerFleetManager::SendFleetSync(int64_t iClientId, const std::vector<Fleet>& rFleets)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kVerbose, "ServerFleetManager::SendFleetSync Client: {} Fleets: {}", iClientId, rFleets.size());

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// [1B type][8B fleetCount] per fleet: [8B guid.uiHigh][8B guid.uiLow][8B memberCount][8B iFlagshipIndex][4B navigationDelay] per member: [8B globalPlayerId][1B bAlive]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerFleetSync));
	rWorkbuffer.PushBack<int64_t>(std::ssize(rFleets));
	for (const Fleet& rFleet : rFleets)
	{
		rWorkbuffer.PushBack<uint64_t>(rFleet.guid.uiHigh);
		rWorkbuffer.PushBack<uint64_t>(rFleet.guid.uiLow);
		rWorkbuffer.PushBack<int64_t>(std::ssize(rFleet.members));
		rWorkbuffer.PushBack<int64_t>(rFleet.iFlagshipIndex);
		rWorkbuffer.PushBack<float>(rFleet.fNavigationDelay);
		for (const FleetMember& rMember : rFleet.members)
		{
			rWorkbuffer.PushBack<int64_t>(rMember.globalPlayerId.iValue);
			rWorkbuffer.PushBack<uint8_t>(rMember.bAlive ? 1 : 0);
		}
	}

	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void ServerFleetManager::QueueCreateRequest(const PendingCreateFleetRequest& rRequest)
{
	mPendingCreateFleetRequests.push_back(rRequest);
}

void ServerFleetManager::QueueDeleteRequest(const PendingDeleteFleetRequest& rRequest)
{
	mPendingDeleteFleetRequests.push_back(rRequest);
}

void ServerFleetManager::QueueSpawnIntoRequest(const PendingSpawnIntoFleetRequest& rRequest)
{
	mPendingSpawnIntoFleetRequests.push_back(rRequest);
}

void ServerFleetManager::QueueRespawnRequest(const PendingRespawnInFleetRequest& rRequest)
{
	mPendingRespawnInFleetRequests.push_back(rRequest);
}

void ServerFleetManager::ClearPendingRequests()
{
	mPendingCreateFleetRequests.clear();
	mPendingDeleteFleetRequests.clear();
	mPendingSpawnIntoFleetRequests.clear();
	mPendingRespawnInFleetRequests.clear();
}

void ServerFleetManager::ShiftFlagshipAfterDeath(const engine::ClientGuid& rGuid, int64_t iFleetIndex, Fleet& rFleet)
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
	mPendingFlagshipUpdates.push_back({rGuid, iFleetIndex, rFleet.wantedCoord});
}

void ServerFleetManager::OnPlayerDeath(const engine::ClientGuid& rGuid, engine::global_id_t globalId)
{
	auto fleetIt = mFleets.find(rGuid);
	if (fleetIt == mFleets.end())
	{
		return;
	}

	for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
	{
		Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleet));
		for (int64_t j = 0; j < std::ssize(rFleet.members); ++j)
		{
			if (rFleet.members.at(j).globalPlayerId == globalId && rFleet.members.at(j).bAlive)
			{
				rFleet.members.at(j).bAlive = false;

				if (j == rFleet.iFlagshipIndex)
				{
					ShiftFlagshipAfterDeath(rGuid, iFleet, rFleet);
				}

				// Send fleet sync only if client is connected
				int64_t iClientId = FindClientIdForGuid(rGuid);
				if (iClientId != 0)
				{
					SendFleetSyncToClient(iClientId);
				}
				return;
			}
		}
	}
}

void ServerFleetManager::OnPlayerSpawned(int64_t iClientId, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId)
{
	if (rSpawnInfo.iFleetIndex < 0)
	{
		return;
	}

	// Heap: try_emplace fleet entry, fleet members vector grows, pending flagship update
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	engine::ClientGuid guid = FindGuidForClient(iClientId);
	std::vector<Fleet>& rFleets = mFleets.try_emplace(guid).first->second;
	if (rSpawnInfo.iFleetIndex >= std::ssize(rFleets))
	{
		return;
	}

	Fleet& rFleet = rFleets.at(static_cast<size_t>(rSpawnInfo.iFleetIndex));
	if (rSpawnInfo.iMemberIndex >= 0 && rSpawnInfo.iMemberIndex < std::ssize(rFleet.members))
	{
		// Respawn: replace dead member
		rFleet.members.at(static_cast<size_t>(rSpawnInfo.iMemberIndex)) = FleetMember {globalPlayerId, true, engine::kOriginCoord};
	}
	else
	{
		// New member
		rFleet.members.push_back(FleetMember {globalPlayerId, true, engine::kOriginCoord});
	}

	mPlayerToGuid.insert_or_assign(globalPlayerId, guid);

	SendFleetSyncToClient(iClientId);

	// Queue flagship update if this member is or becomes the Flagship
	int64_t iThisMemberIndex = (rSpawnInfo.iMemberIndex >= 0)
		? rSpawnInfo.iMemberIndex
		: std::ssize(rFleet.members) - 1;
	bool bHasAliveFlagship = rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive &&
		iThisMemberIndex != rFleet.iFlagshipIndex;
	if (!bHasAliveFlagship)
	{
		rFleet.iFlagshipIndex = iThisMemberIndex;
		rFleet.wantedCoord = engine::kOriginCoord;
		rFleet.fFrameChangeTimer = common::Random(rFleet.fNavigationDelay, mRandomEngine);
		mPendingFlagshipUpdates.push_back({guid, rSpawnInfo.iFleetIndex, rFleet.wantedCoord});
	}
}

void ServerFleetManager::OnPlayerTransferred(const engine::ClientGuid& rGuid, engine::global_id_t globalPlayerId, engine::GridCoord destination)
{
	auto fleetIt = mFleets.find(rGuid);
	if (fleetIt == mFleets.end())
	{
		return;
	}

	for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
	{
		Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleet));
		for (FleetMember& rMember : rFleet.members)
		{
			if (rMember.globalPlayerId == globalPlayerId)
			{
				rMember.coord = destination;
				return;
			}
		}
	}
}

void ServerFleetManager::OnClientConnected(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	// Heap: insert_or_assign mGuidToClientId, try_emplace owned-id vector, SendFleetSync packet
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mGuidToClientId.insert_or_assign(rClientGuid, iClientId);

	const std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;

	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt != mFleets.end())
	{
		// Update alive flags and member coords from re-linked players
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		for (Fleet& rFleet : fleetIt->second)
		{
			for (FleetMember& rMember : rFleet.members)
			{
				rMember.bAlive = std::ranges::contains(rOwnedIds, rMember.globalPlayerId);
				// Update coord from authorizedCoords if client is available
				if (pClient != nullptr)
				{
					for (int64_t k = 0; k < std::ssize(rOwnedIds); ++k)
					{
						if (rOwnedIds.at(k) == rMember.globalPlayerId)
						{
							rMember.coord = pClient->authorizedCoords.at(k);
							break;
						}
					}
				}
			}
		}
	}
	SendFleetSyncToClient(iClientId);
}

void ServerFleetManager::OnClientDisconnected([[maybe_unused]] int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	// Mark as disconnected — fleet data stays in mFleets
	auto it = mGuidToClientId.find(rClientGuid);
	if (it != mGuidToClientId.end())
	{
		it->second = 0;
	}
}

void ServerFleetManager::OnResetForLoad(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	// Heap: try_emplace owned-id, mPlayerToGuid rebuild, pending flagship updates after load
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);

	mGuidToClientId.insert_or_assign(rClientGuid, iClientId);

	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt != mFleets.end())
	{
		for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
		{
			Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleet));
			for (FleetMember& rMember : rFleet.members)
			{
				rMember.bAlive = std::ranges::contains(rOwnedIds, rMember.globalPlayerId);
				// Update coord from authorizedCoords if client is available
				if (pClient != nullptr)
				{
					for (int64_t k = 0; k < std::ssize(rOwnedIds); ++k)
					{
						if (rOwnedIds.at(k) == rMember.globalPlayerId)
						{
							rMember.coord = pClient->authorizedCoords.at(k);
							break;
						}
					}
				}
			}

			// Shift flagship to next alive member if current flagship is dead
			if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
				!rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
			{
				ShiftFlagshipAfterDeath(rClientGuid, iFleet, rFleet);
			}
			else if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
				rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
			{
				// Flagship still alive — set wantedCoord and queue update
				rFleet.wantedCoord = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).coord;
				rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
				mPendingFlagshipUpdates.push_back({rClientGuid, iFleet, rFleet.wantedCoord});
			}
		}

		// Rebuild mPlayerToGuid for this fleet's members
		for (const Fleet& rFleet : fleetIt->second)
		{
			for (const FleetMember& rMember : rFleet.members)
			{
				if (rMember.bAlive)
				{
					mPlayerToGuid.insert_or_assign(rMember.globalPlayerId, rClientGuid);
				}
			}
		}
	}
	SendFleetSyncToClient(iClientId);
}

ServerFleetManager::FleetLookupResult ServerFleetManager::LookupFleetWantedCoord(int64_t iClientId, int64_t iFleetIndex, int64_t iMemberIndex)
{
	engine::ClientGuid guid = FindGuidForClient(iClientId);
	auto fleetIt = mFleets.find(guid);
	if (fleetIt == mFleets.end() || iFleetIndex >= std::ssize(fleetIt->second))
	{
		return {};
	}

	const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleetIndex));
	bool bIsFlagship = (iMemberIndex == rFleet.iFlagshipIndex) ||
		(iMemberIndex < 0 && rFleet.members.empty());

	return {bIsFlagship, rFleet.wantedCoord, rFleet.uiPendingFleetWantedCoordTicks};
}

void ServerFleetManager::DetectDisconnectedPlayerDeaths()
{
	// Heap: ShiftFlagshipAfterDeath may push pending-flagship-update entries
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (auto& [rGuid, rFleets] : mFleets)
	{
		// Skip connected clients (handled by existing DetectPlayerDeaths)
		int64_t iClientId = FindClientIdForGuid(rGuid);
		if (iClientId != 0)
		{
			continue;
		}

		for (int64_t iFleet = 0; iFleet < std::ssize(rFleets); ++iFleet)
		{
			Fleet& rFleet = rFleets.at(static_cast<size_t>(iFleet));
			for (int64_t j = 0; j < std::ssize(rFleet.members); ++j)
			{
				FleetMember& rMember = rFleet.members.at(j);
				if (!rMember.bAlive)
				{
					continue;
				}

				if (!gpGame->mCoordFrames.contains(rMember.coord))
				{
					continue;
				}

				const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(rMember.coord).postRender.pPlayers;
				bool bFound = false;
				for (int64_t k = 0; k < rPlayers.iCount; ++k)
				{
					if (rPlayers.pGlobalPlayerIds[k] == rMember.globalPlayerId)
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					rMember.bAlive = false;
					if (j == rFleet.iFlagshipIndex)
					{
						ShiftFlagshipAfterDeath(rGuid, iFleet, rFleet);
					}
				}
			}
		}
	}
}

void ServerFleetManager::WriteFleetData(std::fstream& rFileStream) const
{
	int64_t iFleetOwnerCount = std::ssize(mFleets);
	common::Write(rFileStream, iFleetOwnerCount);

	for (const auto& [rGuid, rFleets] : mFleets)
	{
		common::Write(rFileStream, rGuid.uiHigh);
		common::Write(rFileStream, rGuid.uiLow);
		int64_t iFleetCount = std::ssize(rFleets);
		common::Write(rFileStream, iFleetCount);
		for (const Fleet& rFleet : rFleets)
		{
			common::Write(rFileStream, rFleet.guid.uiHigh);
			common::Write(rFileStream, rFleet.guid.uiLow);
			int64_t iMemberCount = std::ssize(rFleet.members);
			common::Write(rFileStream, iMemberCount);
			common::Write(rFileStream, rFleet.iFlagshipIndex);
			common::Write(rFileStream, rFleet.wantedCoord.x);
			common::Write(rFileStream, rFleet.wantedCoord.y);
			common::Write(rFileStream, rFleet.uiPendingFleetWantedCoordTicks);
			common::Write(rFileStream, rFleet.fNavigationDelay);
			common::Write(rFileStream, rFleet.fFrameChangeTimer);
			for (const FleetMember& rMember : rFleet.members)
			{
				common::Write(rFileStream, rMember.globalPlayerId.iValue);
				uint8_t uiAlive = rMember.bAlive ? 1 : 0;
				common::Write(rFileStream, uiAlive);
				common::Write(rFileStream, rMember.coord.x);
				common::Write(rFileStream, rMember.coord.y);
			}
		}
	}

	common::Write(rFileStream, mRandomEngine.uiState);
}

void ServerFleetManager::ReadFleetData(std::fstream& rFileStream)
{
	// Heap: rebuild mFleets/mPlayerToGuid/mGuidToClientId from save stream
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mFleets.clear();
	mPlayerToGuid.clear();
	mGuidToClientId.clear();

	int64_t iFleetOwnerCount = 0;
	common::Read(rFileStream, iFleetOwnerCount);
	for (int64_t i = 0; i < iFleetOwnerCount; ++i)
	{
		uint64_t uiGuidHigh = 0;
		uint64_t uiGuidLow = 0;
		common::Read(rFileStream, uiGuidHigh);
		common::Read(rFileStream, uiGuidLow);
		engine::ClientGuid guid {uiGuidHigh, uiGuidLow};
		int64_t iFleetCount = 0;
		common::Read(rFileStream, iFleetCount);
		std::vector<Fleet> fleets(static_cast<size_t>(iFleetCount));
		for (int64_t j = 0; j < iFleetCount; ++j)
		{
			Fleet& rFleet = fleets.at(static_cast<size_t>(j));
			common::Read(rFileStream, rFleet.guid.uiHigh);
			common::Read(rFileStream, rFleet.guid.uiLow);
			int64_t iMemberCount = 0;
			common::Read(rFileStream, iMemberCount);
			common::Read(rFileStream, rFleet.iFlagshipIndex);
			int32_t iWantedX = 0;
			int32_t iWantedY = 0;
			common::Read(rFileStream, iWantedX);
			common::Read(rFileStream, iWantedY);
			rFleet.wantedCoord = engine::GridCoord {iWantedX, iWantedY};
			common::Read(rFileStream, rFleet.uiPendingFleetWantedCoordTicks);
			common::Read(rFileStream, rFleet.fNavigationDelay);
			common::Read(rFileStream, rFleet.fFrameChangeTimer);
			rFleet.members.resize(static_cast<size_t>(iMemberCount));
			for (int64_t k = 0; k < iMemberCount; ++k)
			{
				int64_t iGlobalPlayerId = 0;
				common::Read(rFileStream, iGlobalPlayerId);
				uint8_t uiAlive = 0;
				common::Read(rFileStream, uiAlive);
				int32_t iCoordX = 0;
				int32_t iCoordY = 0;
				common::Read(rFileStream, iCoordX);
				common::Read(rFileStream, iCoordY);
				rFleet.members.at(static_cast<size_t>(k)) = FleetMember {engine::global_id_t {iGlobalPlayerId}, uiAlive != 0, engine::GridCoord {iCoordX, iCoordY}};

				// Rebuild reverse lookup
				if (uiAlive != 0)
				{
					mPlayerToGuid.insert_or_assign(engine::global_id_t {iGlobalPlayerId}, guid);
				}
			}
		}
		mFleets.insert_or_assign(guid, std::move(fleets));
		// All loaded fleets start as disconnected
		mGuidToClientId.insert_or_assign(guid, static_cast<int64_t>(0));
	}

	common::Read(rFileStream, mRandomEngine.uiState);
}

void ServerFleetManager::UpdateFleetNavigationDelay(const engine::ClientGuid& rGuid, int64_t iFleetIndex, float fDelay)
{
	auto fleetIt = mFleets.find(rGuid);
	if (fleetIt == mFleets.end() || iFleetIndex < 0 || iFleetIndex >= std::ssize(fleetIt->second))
	{
		return;
	}

	fleetIt->second.at(static_cast<size_t>(iFleetIndex)).fNavigationDelay = fDelay;
	LOG(kNetwork, kDebug, "ServerFleetManager::UpdateFleetNavigationDelay Guid: ({},{}) Fleet: {} Delay: {}", rGuid.uiHigh, rGuid.uiLow, iFleetIndex, fDelay);

	// Resync fleet to client so UI updates
	int64_t iClientId = FindClientIdForGuid(rGuid);
	if (iClientId != 0)
	{
		SendFleetSyncToClient(iClientId);
	}
}

void ServerFleetManager::ResetState()
{
	// mRandomEngine intentionally not re-seeded: constructor TimeSeeds once; ReadFleetData restores from save for replay determinism.
	mPendingCreateFleetRequests.clear();
	mPendingDeleteFleetRequests.clear();
	mPendingSpawnIntoFleetRequests.clear();
	mPendingRespawnInFleetRequests.clear();
	mFleets.clear();
	mPlayerToGuid.clear();
	mGuidToClientId.clear();
}

#endif // BT_SERVER

} // namespace game
