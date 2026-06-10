#include "Pch.h"

#include "Network/Server/ServerFleetManager.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerFleetManagerUtils.h"
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
	ScopedSuppressAllocationTracking suppress;

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
	ScopedSuppressAllocationTracking suppress;

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
	ScopedSuppressAllocationTracking suppress;

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
	ScopedSuppressAllocationTracking suppress;

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

void ServerFleetManager::TickFleetTimers()
{
	// Heap: pending flagship updates vector grows when flagship picks new direction
	ScopedSuppressAllocationTracking suppress;

	mNavigation.TickFleetTimers(mFleets, mRandomEngine);
}

void ServerFleetManager::ProcessFlagshipUpdates()
{
	// Heap: per-member statusChanges vector grows on flagship update broadcast
	ScopedSuppressAllocationTracking suppress;

	mNavigation.ProcessFlagshipUpdates(mFleets);
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
	::game::SendFleetSync(iClientId, rFleets);
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
					mNavigation.ShiftFlagshipAfterDeath(rGuid, iFleet, rFleet);
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
	ScopedSuppressAllocationTracking suppress;

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
		mNavigation.QueueFlagshipUpdate({.clientGuid = guid, .iFleetIndex = rSpawnInfo.iFleetIndex, .newWantedCoord = rFleet.wantedCoord});
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
	ScopedSuppressAllocationTracking suppress;

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
	ScopedSuppressAllocationTracking suppress;

	const std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(iClientId).first->second;
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);

	mGuidToClientId.insert_or_assign(rClientGuid, iClientId);

	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt != mFleets.end())
	{
		for (int64_t iFleet = 0; iFleet < std::ssize(fleetIt->second); ++iFleet)
		{
			ResetFleetForLoad(fleetIt->second.at(static_cast<size_t>(iFleet)), rClientGuid, iFleet, rOwnedIds, pClient);
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

void ServerFleetManager::ResetFleetForLoad(Fleet& rFleet, const engine::ClientGuid& rClientGuid, int64_t iFleetIndex, const std::vector<engine::global_id_t>& rOwnedIds, const engine::ClientConnection* pClient)
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

	// Shift flagship to next alive member if current flagship is dead
	if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		!rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
	{
		mNavigation.ShiftFlagshipAfterDeath(rClientGuid, iFleetIndex, rFleet);
	}
	else if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
	{
		// Flagship still alive — set wantedCoord and queue update
		rFleet.wantedCoord = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).coord;
		rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
		mNavigation.QueueFlagshipUpdate({.clientGuid = rClientGuid, .iFleetIndex = iFleetIndex, .newWantedCoord = rFleet.wantedCoord});
	}
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

	return {.bIsFlagship = bIsFlagship, .fleetWantedCoord = rFleet.wantedCoord, .uiPendingFleetWantedCoordTicks = rFleet.uiPendingFleetWantedCoordTicks};
}

void ServerFleetManager::DetectDisconnectedPlayerDeaths()
{
	// Heap: ShiftFlagshipAfterDeath may push pending-flagship-update entries
	ScopedSuppressAllocationTracking suppress;

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
						mNavigation.ShiftFlagshipAfterDeath(rGuid, iFleet, rFleet);
					}
				}
			}
		}
	}
}

void ServerFleetManager::WriteFleetData(std::fstream& rFileStream) const
{
	::game::WriteFleetData(rFileStream, mFleets, mRandomEngine);
}

void ServerFleetManager::ReadFleetData(std::fstream& rFileStream)
{
	// Heap: rebuild mFleets/mPlayerToGuid/mGuidToClientId from save stream
	ScopedSuppressAllocationTracking suppress;

	::game::ReadFleetData(rFileStream, mFleets, mPlayerToGuid, mGuidToClientId, mRandomEngine);
}

void ServerFleetManager::UpdateFleetNavigationDelay(const engine::ClientGuid& rGuid, int64_t iFleetIndex, float fDelay)
{
	auto fleetIt = mFleets.find(rGuid);
	if (fleetIt == mFleets.end() || iFleetIndex < 0 || iFleetIndex >= std::ssize(fleetIt->second))
	{
		return;
	}

	fleetIt->second.at(static_cast<size_t>(iFleetIndex)).fNavigationDelay = fDelay;
	LOG(kNetwork, kDebug, "ServerFleetManager::UpdateFleetNavigationDelay Guid: ({},{}) Fleet: {} Delay: {}", rGuid.uiHigh, rGuid.uiLow, iFleetIndex, common::Wb(fDelay, 3));

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
