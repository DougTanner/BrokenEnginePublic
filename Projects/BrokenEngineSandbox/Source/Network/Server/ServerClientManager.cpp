#include "Pch.h"

#include "Network/Server/ServerClientManager.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/PlayerEvents.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerSession.h"
#include "Network/Server/ServerTransferManager.h"

namespace game
{

#if defined(BT_SERVER)

void ServerClientManager::QueueSpawnForClient(int64_t iClientId, const engine::ClientGuid& rClientGuid, const FleetGuid& rFleetGuid, int64_t iMemberIndex)
{
	// A queued spawn revives the client: clear its dead/processed state unconditionally.
	mDeadClientIds.erase(iClientId);
	mProcessedClientIds.erase(iClientId);

	// Dedup only the queue push on the full spawn identity so a client spamming spawn-into/respawn queues at most one spawn
	// per (fleet guid, member). Skip duplicates without reordering — preserves the order-sensitive spawn-assignment invariant.
	bool bAlreadyQueued = std::ranges::any_of(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
	{
		return rInfo.iClientId == iClientId && rInfo.fleetGuid == rFleetGuid && rInfo.iMemberIndex == iMemberIndex;
	});
	if (!bAlreadyQueued)
	{
		mClientsWaitingForSpawn.push_back({iClientId, rClientGuid, rFleetGuid, iMemberIndex});
	}
}

void ServerClientManager::NewClients()
{
	// Heap: vector push_back for waiting clients
	ScopedSuppressAllocationTracking suppress;

	std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
	for (engine::ClientConnection& rClient : rClients)
	{
		if (!rClient.bHandshakeComplete)
		{
			continue;
		}

		if (!gpServerSession->mClientPlayers.Owned(rClient.iClientId).empty())
		{
			continue;
		}

		if (mDeadClientIds.contains(rClient.iClientId))
		{
			continue;
		}

		if (mProcessedClientIds.contains(rClient.iClientId))
		{
			continue;
		}

		if (std::ranges::contains(mClientsWaitingForSpawn, rClient.iClientId, &ClientSpawnInfo::iClientId))
		{
			continue;
		}

		LogConnectingClientDiagnostic(rClient);

		if (gpServerSession->mClientPlayers.RelinkFromFrames(rClient.iClientId, rClient.clientGuid, ClientPlayerRegistry::RelinkContext::kConnect) > 0)
		{
			gpServerSession->mpFleetManager->OnClientConnected(rClient.iClientId, rClient.clientGuid);
			continue;
		}

		// Client connects with zero players — spawns happen via fleet creation requests
		mProcessedClientIds.insert(rClient.iClientId);
		LOG(kNetwork, kVerbose, "ServerClientManager::NewClients Client: {} connected with no players", rClient.iClientId);
	}
}

void ServerClientManager::LogConnectingClientDiagnostic(const engine::ClientConnection& rClient)
{
	// Diagnostic: dump connecting GUID, server-side fleet roster, and per-coord player GUIDs so we can see whether reconnect should re-link
	LOG(kNetwork, kInfo, "ServerClientManager::NewClients Connecting Client: {} Guid: ({},{}) Empty: {}",
		rClient.iClientId, rClient.clientGuid.uiHigh, rClient.clientGuid.uiLow, rClient.clientGuid.IsEmpty());
	LOG(kNetwork, kInfo, "  FleetGuids: {}", gpServerSession->mpFleetManager->mFleets.size());
	for (const auto& [rExistingGuid, rExistingFleets] : gpServerSession->mpFleetManager->mFleets)
	{
		LOG(kNetwork, kInfo, "    Guid: ({},{}) FleetCount: {} Match: {}",
			rExistingGuid.uiHigh, rExistingGuid.uiLow, rExistingFleets.size(), rExistingGuid == rClient.clientGuid);
	}
	for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		const PlayersPostRender& rPlayers = *rFrames.pCurrent->postRender.pPlayers;
		if (rPlayers.iCount == 0)
		{
			continue;
		}
		LOG(kNetwork, kInfo, "  Coord: ({},{}) PlayerCount: {}", rCoord.x, rCoord.y, rPlayers.iCount);
		for (int64_t i = 0; i < rPlayers.iCount; ++i)
		{
			LOG(kNetwork, kInfo, "    Global: {} Guid: ({},{}) Match: {}",
				rPlayers.pGlobalPlayerIds[i].iValue, rPlayers.pClientGuids[i].uiHigh, rPlayers.pClientGuids[i].uiLow,
				rPlayers.pClientGuids[i] == rClient.clientGuid);
		}
	}
}

void ServerClientManager::FinalizeNewClients()
{
	if (mClientsWaitingForSpawn.empty())
	{
		return;
	}

	// Heap: vector operations
	ScopedSuppressAllocationTracking suppress;

	// Find newly spawned player IDs (present now but not in pre-spawn snapshot)
	const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	std::vector<player_t> newPlayerIds;
	newPlayerIds.reserve(static_cast<size_t>(rPlayers.iCount));
	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		if (!std::ranges::contains(mPreSpawnPlayerIds, rPlayers.puiIds[i]))
		{
			newPlayerIds.push_back(rPlayers.puiIds[i]);
		}
	}

	LOG(kNetwork, kVerbose, "ServerClientManager::FinalizeNewClients Waiting: {} PlayerCount: {} PreSpawn: {} NewIds: {}", mClientsWaitingForSpawn.size(), rPlayers.iCount, mPreSpawnPlayerIds.size(), newPlayerIds.size());
	// Assign new players to waiting clients (in order)
	// Client handles subscriptions — no full state sent here
	size_t uiAssignCount = std::min(mClientsWaitingForSpawn.size(), newPlayerIds.size());
	for (size_t i = 0; i < uiAssignCount; ++i)
	{
		int64_t iClientId = mClientsWaitingForSpawn.at(i).iClientId;
		player_t playerId = newPlayerIds.at(i);

		// Find the player's index and read its global ID
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		PlayersPostRender& rPlayersPostRender = *gpGame->CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
		const PlayersInterpolate& rPlayersInterpolate = *gpGame->CurrentFrame(engine::kOriginCoord).interpolate.pPlayers;
		if (rPlayersInterpolate.idToIndexMap.contains(playerId))
		{
			int64_t iPlayerIndex = rPlayersInterpolate.idToIndexMap.at(playerId);
			engine::global_id_t globalPlayerId = rPlayersPostRender.pGlobalPlayerIds[iPlayerIndex];

			gpServerSession->SendAssignPlayer(iClientId, globalPlayerId, engine::kOriginCoord);
			gpServerSession->SendPlayerState(iClientId, PlayerStateWireType::kSpawned, globalPlayerId.iValue, engine::kOriginCoord);

			// Write client GUID into the player entity for save/load re-linking
			if (pClient != nullptr)
			{
				rPlayersPostRender.pClientGuids[iPlayerIndex] = pClient->clientGuid;
				gpServerSession->mClientPlayers.Add(pClient->iClientId, globalPlayerId, engine::kOriginCoord);

				// Associate with fleet if this spawn was fleet-triggered
				const ClientSpawnInfo& rSpawnInfo = mClientsWaitingForSpawn.at(i);
				gpServerSession->mpFleetManager->OnPlayerSpawned(iClientId, pClient->clientGuid, rSpawnInfo, globalPlayerId);
			}
		}
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(uiAssignCount));

	// Refresh snapshot for subsequent ticks
	RefreshPreSpawnSnapshot();
}

void ServerClientManager::Disconnects()
{
	// Heap: drain disconnect events; registry and fleet-manager bookkeeping
	ScopedSuppressAllocationTracking suppress;

	for (const engine::PendingDisconnect& rDisconnect : gpServerSession->mpRuntime->mpServer->mPendingDisconnects)
	{
		LOG(kNetwork, kVerbose, "ServerClientManager::Disconnects Client: {} Players: {}", rDisconnect.iClientId, gpServerSession->mClientPlayers.Owned(rDisconnect.iClientId).size());
		mDeadClientIds.erase(rDisconnect.iClientId);
		mProcessedClientIds.erase(rDisconnect.iClientId);

		gpServerSession->mpFleetManager->OnClientDisconnected(rDisconnect.clientGuid);

		// Remove from spawn queue if waiting
		std::erase_if(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
		{
			return rInfo.iClientId == rDisconnect.iClientId;
		});

		gpServerSession->mClientPlayers.Remove(rDisconnect.iClientId);
	}

	// Unpause and reset timespeed when the last client disconnects so the server resumes ticking at 1x for the next
	// connection. State-tracked on the non-empty -> empty transition (not mClients.empty() alone, which would
	// clobber commanded state every update) so the edge is caught regardless of removal path — including ClientHello
	// rejects that call Server::RemoveClient without minting a PendingDisconnect. An agent-paused server that was
	// already empty keeps its commanded pause/timescale.
	bool bHasClients = !engine::gpServer->mClients.empty();
	if (mbHadClients && !bHasClients)
	{
		gpGame->mGameFlags.Clear(engine::GameFlags::kPaused);
		if (gpGame->mTimeStep.miTimeMultiply != 1 || gpGame->mTimeStep.miTimeDivide != 1)
		{
			gpGame->mTimeStep.SetTimeScale(1, 1);
		}
	}
	mbHadClients = bHasClients;
}

void ServerClientManager::DetectPlayerDeaths()
{
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
	for (engine::ClientConnection& rClient : rClients)
	{
		std::span<const OwnedPlayer> ownedPlayers = gpServerSession->mClientPlayers.Owned(rClient.iClientId);

		if (ownedPlayers.empty())
		{
			continue;
		}

		if (mDeadClientIds.contains(rClient.iClientId))
		{
			continue;
		}

		// Skip clients mid-transfer (subscription update pending from HarvestTransfers)
		if (gpServerSession->mpTransferManager->HasPendingSubscriptionUpdate(rClient.iClientId))
		{
			continue;
		}

		// Heap: per-frame death scan may erase registry entries and authorized coords
		ScopedSuppressAllocationTracking suppress;

		// Check each owned player for death (reverse iterate for safe removal)
		for (int64_t i = std::ssize(ownedPlayers) - 1; i >= 0; --i)
		{
			const OwnedPlayer& rOwnedPlayer = ownedPlayers[i];
			engine::global_id_t globalId = rOwnedPlayer.globalId;
			engine::GridCoord coord = rOwnedPlayer.coord;

			if (!gpGame->mCoordFrames.contains(coord))
			{
				continue;
			}

			// Scan pGlobalPlayerIds to see if the player still exists
			const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(coord).postRender.pPlayers;
			bool bFound = false;
			for (int64_t j = 0; j < rPlayers.iCount; ++j)
			{
				if (rPlayers.pGlobalPlayerIds[j] == globalId)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				gpServerSession->SendPlayerState(rClient.iClientId, PlayerStateWireType::kDied, globalId.iValue, coord);
				LOG(kNetwork, kVerbose, "ServerClientManager::DetectPlayerDeaths Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId, coord.x, coord.y);
				gpServerSession->mClientPlayers.RemoveAt(rClient.iClientId, i);

				gpServerSession->mpFleetManager->OnPlayerDeath(rClient.clientGuid, globalId);
			}
		}

		// Mark client as dead only when ALL owned players are dead
		if (gpServerSession->mClientPlayers.Owned(rClient.iClientId).empty())
		{
			mDeadClientIds.insert(rClient.iClientId);
		}
	}
}

void ServerClientManager::RefreshPreSpawnSnapshot()
{
	mPreSpawnPlayerIds.clear();
	const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	mPreSpawnPlayerIds.reserve(static_cast<size_t>(rPlayers.iCount));
	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		mPreSpawnPlayerIds.push_back(rPlayers.puiIds[i]);
	}
}

void ServerClientManager::ResetState()
{
	mClientsWaitingForSpawn.clear();
	mDeadClientIds.clear();
	mProcessedClientIds.clear();
	mPreSpawnPlayerIds.clear();
}

#endif // BT_SERVER

} // namespace game
