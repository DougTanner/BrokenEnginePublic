#include "Pch.h"

#include "Network/Server/ServerFleetManager.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerFleetSerialization.h"
#include "Network/Server/ServerSession.h"

namespace game
{

#if defined(BT_SERVER)

// DoS ceiling on per-client fleet count — well above any real use; bounds mFleets against a spamming client.
constexpr int64_t kiMaxFleetsPerClient = 16;
// Per-fleet member cap — parity with Frame.cpp's kiMaxFleetSize (16); bounds Fleet::members against a spamming client.
constexpr size_t kiMaxFleetMembers = 16;

ServerFleetManager::ServerFleetManager()
{
	mRandomEngine.TimeSeed();
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

int64_t ServerFleetManager::FindFleetIndexByGuid(const std::vector<Fleet>& rFleets, const FleetGuid& rFleetGuid) const
{
	for (int64_t i = 0; i < std::ssize(rFleets); ++i)
	{
		if (rFleets.at(static_cast<size_t>(i)).guid == rFleetGuid)
		{
			return i;
		}
	}
	return -1;
}

void ServerFleetManager::ProcessCreateFleetRequests()
{
	// Heap: mFleets map and fleet-list grow on create
	ScopedSuppressAllocationTracking suppress;

	for (const PendingCreateFleetRequest& rRequest : mPendingCreateFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		auto it = mFleets.find(guid);
		if (it != mFleets.end() && std::ssize(it->second) >= kiMaxFleetsPerClient)
		{
			LOG(kNetwork, kWarning, "ServerFleetManager::ProcessCreateFleetRequests Client: {} at fleet cap {}, ignoring create", rRequest.iClientId, kiMaxFleetsPerClient);
			continue;
		}
		Fleet& rNewFleet = mFleets.try_emplace(guid).first->second.emplace_back();
		rNewFleet.guid.uiHigh = common::RandomNext(mRandomEngine);
		rNewFleet.guid.uiLow = common::RandomNext(mRandomEngine);
		mGuidToClientId.insert_or_assign(guid, rRequest.iClientId);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessCreateFleetRequests Client: {} FleetCount: {} FleetGuid: ({},{})", rRequest.iClientId, mFleets.at(guid).size(), rNewFleet.guid.uiHigh, rNewFleet.guid.uiLow);
		SendFleetSyncToClient(rRequest.iClientId, guid);
	}

	// Drain on process: AfterNetworkPoll runs twice per update, and a retained request would create a
	// second fleet on the tick-boundary poll.
	mPendingCreateFleetRequests.clear();
}

void ServerFleetManager::ProcessDeleteFleetRequests()
{
	for (const PendingDeleteFleetRequest& rRequest : mPendingDeleteFleetRequests)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		engine::ClientGuid guid = pClient->clientGuid;
		auto it = mFleets.find(guid);
		if (it == mFleets.end())
		{
			continue;
		}

		int64_t iFleetIndex = FindFleetIndexByGuid(it->second, rRequest.fleetGuid);
		if (iFleetIndex < 0)
		{
			continue;
		}

		Fleet& rFleet = it->second.at(static_cast<size_t>(iFleetIndex));
		if (!rFleet.members.empty())
		{
			continue;
		}

		it->second.erase(it->second.begin() + iFleetIndex);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessDeleteFleetRequests Client: {} FleetGuid: ({},{}) FleetCount: {}", rRequest.iClientId, rRequest.fleetGuid.uiHigh, rRequest.fleetGuid.uiLow, it->second.size());
		SendFleetSyncToClient(rRequest.iClientId, guid);
	}

	mPendingDeleteFleetRequests.clear();
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
		if (it == mFleets.end())
		{
			continue;
		}

		int64_t iFleetIndex = FindFleetIndexByGuid(it->second, rRequest.fleetGuid);
		if (iFleetIndex < 0)
		{
			continue;
		}

		const Fleet& rFleet = it->second.at(static_cast<size_t>(iFleetIndex));
		if (rFleet.members.size() >= kiMaxFleetMembers)
		{
			LOG(kNetwork, kWarning, "ServerFleetManager::ProcessSpawnIntoFleetRequests Client: {} FleetGuid: ({},{}) at member cap {}, ignoring spawn", rRequest.iClientId, rRequest.fleetGuid.uiHigh, rRequest.fleetGuid.uiLow, kiMaxFleetMembers);
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, guid, rRequest.fleetGuid, -1);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessSpawnIntoFleetRequests Client: {} FleetGuid: ({},{})", rRequest.iClientId, rRequest.fleetGuid.uiHigh, rRequest.fleetGuid.uiLow);
	}

	mPendingSpawnIntoFleetRequests.clear();
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
		if (it == mFleets.end())
		{
			continue;
		}

		int64_t iFleetIndex = FindFleetIndexByGuid(it->second, rRequest.fleetGuid);
		if (iFleetIndex < 0)
		{
			continue;
		}

		const Fleet& rFleet = it->second.at(static_cast<size_t>(iFleetIndex));
		if (rRequest.iMemberIndex < 0 || rRequest.iMemberIndex >= std::ssize(rFleet.members))
		{
			continue;
		}

		if (rFleet.members.at(static_cast<size_t>(rRequest.iMemberIndex)).bAlive)
		{
			continue;
		}

		gpServerSession->mpClientManager->QueueSpawnForClient(rRequest.iClientId, guid, rRequest.fleetGuid, rRequest.iMemberIndex);
		LOG(kNetwork, kDebug, "ServerFleetManager::ProcessRespawnInFleetRequests Client: {} FleetGuid: ({},{}) Member: {}", rRequest.iClientId, rRequest.fleetGuid.uiHigh, rRequest.fleetGuid.uiLow, rRequest.iMemberIndex);
	}

	mPendingRespawnInFleetRequests.clear();
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

void ServerFleetManager::SendFleetSyncToClient(int64_t iClientId, const engine::ClientGuid& rClientGuid)
{
	auto it = mFleets.find(rClientGuid);
	if (it != mFleets.end())
	{
		::game::SendFleetSync(iClientId, it->second);
	}
	else
	{
		::game::SendFleetSync(iClientId, {});
	}
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
					mNavigation.ShiftFlagshipAfterDeath(rGuid, rFleet);
				}

				// Send fleet sync only if client is connected
				int64_t iClientId = FindClientIdForGuid(rGuid);
				if (iClientId != 0)
				{
					SendFleetSyncToClient(iClientId, rGuid);
				}
				return;
			}
		}
	}
}

void ServerFleetManager::OnPlayerSpawned(int64_t iClientId, const engine::ClientGuid& rClientGuid, const ClientSpawnInfo& rSpawnInfo, engine::global_id_t globalPlayerId)
{
	if (!rSpawnInfo.fleetGuid.IsValid())
	{
		return;
	}

	// Heap: try_emplace fleet entry, fleet members vector grows, pending flagship update
	ScopedSuppressAllocationTracking suppress;

	std::vector<Fleet>& rFleets = mFleets.try_emplace(rClientGuid).first->second;
	int64_t iFleetIndex = FindFleetIndexByGuid(rFleets, rSpawnInfo.fleetGuid);
	if (iFleetIndex < 0)
	{
		return;
	}

	Fleet& rFleet = rFleets.at(static_cast<size_t>(iFleetIndex));
	if (rSpawnInfo.iMemberIndex >= 0 && rSpawnInfo.iMemberIndex < std::ssize(rFleet.members))
	{
		// Respawn: replace dead member
		rFleet.members.at(static_cast<size_t>(rSpawnInfo.iMemberIndex)) = FleetMember {globalPlayerId, true, engine::kOriginCoord};
	}
	else
	{
		// New member — cap per-fleet member count so a spamming client can't grow members unboundedly.
		// At cap, drop this spawn from the fleet roster entirely (return before flagship mutation)
		// rather than skip only the push: a phantom member would corrupt flagship assignment.
		if (rFleet.members.size() >= kiMaxFleetMembers)
		{
			LOG(kNetwork, kWarning, "ServerFleetManager::OnPlayerSpawned Client: {} FleetGuid: ({},{}) at member cap {}, ignoring spawn", iClientId, rFleet.guid.uiHigh, rFleet.guid.uiLow, kiMaxFleetMembers);
			return;
		}
		rFleet.members.push_back(FleetMember {globalPlayerId, true, engine::kOriginCoord});
	}

	SendFleetSyncToClient(iClientId, rClientGuid);

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
		mNavigation.QueueFlagshipUpdate({.clientGuid = rClientGuid, .fleetGuid = rFleet.guid, .newWantedCoord = rFleet.wantedCoord});
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
	// Heap: insert_or_assign connected-client mapping
	ScopedSuppressAllocationTracking suppress;

	mGuidToClientId.insert_or_assign(rClientGuid, iClientId);
	std::span<const OwnedPlayer> ownedPlayers = gpServerSession->mClientPlayers.Owned(iClientId);

	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt != mFleets.end())
	{
		for (Fleet& rFleet : fleetIt->second)
		{
			RefreshFleetMembers(rFleet, ownedPlayers);
			if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
				!rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
			{
				mNavigation.ShiftFlagshipAfterDeath(rClientGuid, rFleet);
			}
		}
	}
	SendFleetSyncToClient(iClientId, rClientGuid);
}

void ServerFleetManager::OnClientDisconnected(const engine::ClientGuid& rClientGuid)
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
	// Heap: pending flagship updates after load
	ScopedSuppressAllocationTracking suppress;

	mGuidToClientId.insert_or_assign(rClientGuid, iClientId);

	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt != mFleets.end())
	{
		for (Fleet& rFleet : fleetIt->second)
		{
			ResetFleetForLoad(rFleet, rClientGuid);
		}
	}
	SendFleetSyncToClient(iClientId, rClientGuid);
}

void ServerFleetManager::RefreshFleetMembers(Fleet& rFleet, std::span<const OwnedPlayer> ownedPlayers)
{
	for (FleetMember& rMember : rFleet.members)
	{
		rMember.bAlive = false;
		for (const OwnedPlayer& rOwnedPlayer : ownedPlayers)
		{
			if (rOwnedPlayer.globalId == rMember.globalPlayerId)
			{
				rMember.bAlive = true;
				rMember.coord = rOwnedPlayer.coord;
				break;
			}
		}
	}
}

void ServerFleetManager::ResetFleetForLoad(Fleet& rFleet, const engine::ClientGuid& rClientGuid)
{
	RefreshFleetMembers(rFleet, gpServerSession->mClientPlayers.Owned(FindClientIdForGuid(rClientGuid)));

	// Shift flagship to next alive member if current flagship is dead
	if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		!rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
	{
		mNavigation.ShiftFlagshipAfterDeath(rClientGuid, rFleet);
	}
	else if (rFleet.iFlagshipIndex < std::ssize(rFleet.members) &&
		rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).bAlive)
	{
		// Flagship still alive — set wantedCoord and queue update
		rFleet.wantedCoord = rFleet.members.at(static_cast<size_t>(rFleet.iFlagshipIndex)).coord;
		rFleet.fFrameChangeTimer = rFleet.fNavigationDelay;
		mNavigation.QueueFlagshipUpdate({.clientGuid = rClientGuid, .fleetGuid = rFleet.guid, .newWantedCoord = rFleet.wantedCoord});
	}
}

ServerFleetManager::FleetLookupResult ServerFleetManager::LookupFleetWantedCoord(const engine::ClientGuid& rClientGuid, const FleetGuid& rFleetGuid, int64_t iMemberIndex)
{
	auto fleetIt = mFleets.find(rClientGuid);
	if (fleetIt == mFleets.end())
	{
		return {};
	}

	int64_t iFleetIndex = FindFleetIndexByGuid(fleetIt->second, rFleetGuid);
	if (iFleetIndex < 0)
	{
		return {};
	}

	const Fleet& rFleet = fleetIt->second.at(static_cast<size_t>(iFleetIndex));
	bool bIsFlagship = (iMemberIndex == rFleet.iFlagshipIndex) ||
		(iMemberIndex < 0 && rFleet.members.empty());

	FleetLookupFlags_t flags {FleetLookupFlags::kFound};
	flags.Set(FleetLookupFlags::kIsFlagship, bIsFlagship);
	return {.flags = flags, .fleetWantedCoord = rFleet.wantedCoord, .uiPendingFleetWantedCoordTicks = rFleet.uiPendingFleetWantedCoordTicks};
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
						mNavigation.ShiftFlagshipAfterDeath(rGuid, rFleet);
					}
				}
			}
		}
	}
}

void ServerFleetManager::UpdateFleetNavigationDelay(const engine::ClientGuid& rGuid, const FleetGuid& rFleetGuid, float fDelay)
{
	auto fleetIt = mFleets.find(rGuid);
	if (fleetIt == mFleets.end())
	{
		return;
	}

	int64_t iFleetIndex = FindFleetIndexByGuid(fleetIt->second, rFleetGuid);
	if (iFleetIndex < 0)
	{
		return;
	}

	fleetIt->second.at(static_cast<size_t>(iFleetIndex)).fNavigationDelay = fDelay;
	LOG(kNetwork, kDebug, "ServerFleetManager::UpdateFleetNavigationDelay Guid: ({},{}) FleetGuid: ({},{}) Delay: {}", rGuid.uiHigh, rGuid.uiLow, rFleetGuid.uiHigh, rFleetGuid.uiLow, common::Wb(fDelay, 3));

	// Resync fleet to client so UI updates
	int64_t iClientId = FindClientIdForGuid(rGuid);
	if (iClientId != 0)
	{
		SendFleetSyncToClient(iClientId, rGuid);
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
	mGuidToClientId.clear();
}

#endif // BT_SERVER

} // namespace game
