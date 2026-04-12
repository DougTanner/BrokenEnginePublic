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

void ServerClientManager::QueueSpawnForClient(int64_t iClientId, engine::GridCoord spawnCoord, int64_t iFleetIndex, int64_t iMemberIndex)
{
	mDeadClientIds.erase(iClientId);
	mProcessedClientIds.erase(iClientId);
	mClientsWaitingForSpawn.push_back({iClientId, spawnCoord, iFleetIndex, iMemberIndex});
}

void ServerClientManager::ProcessSpawnRequests()
{
	// Heap: vector push_back for spawn StatusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingSpawnRequest& rRequest : engine::gpServer->DrainPendingSpawnRequests())
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		if (rRequest.flags & engine::ClientRequestFlags::kRespawnRequested ||
		    rRequest.flags & engine::ClientRequestFlags::kSpawnRequested)
		{
			mDeadClientIds.erase(rRequest.iClientId);
			mProcessedClientIds.erase(rRequest.iClientId);
			if (!std::ranges::contains(mClientsWaitingForSpawn, rRequest.iClientId, &ClientSpawnInfo::iClientId))
			{
				mClientsWaitingForSpawn.push_back({rRequest.iClientId, engine::kOriginCoord});
			}
		}
	}
}

void ServerClientManager::NewClients()
{
	// Heap: vector push_back for waiting clients
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		std::vector<engine::global_id_t>& rNewClientOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(rClient.iClientId).first->second;

		if (!rNewClientOwnedIds.empty())
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

		// Re-link with existing players by matching ClientGuid (sorted by global ID to preserve creation order)
		if (!rClient.clientGuid.IsEmpty())
		{
			struct RelinkEntry
			{
				engine::global_id_t globalId {};
				engine::GridCoord coord {};
			};
			std::vector<RelinkEntry> relinkEntries;

			for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
			{
				const PlayersPostRender& rPlayers = *rFrames.pCurrent->postRender.pPlayers;
				for (int64_t i = 0; i < rPlayers.iCount; ++i)
				{
					if (rPlayers.pClientGuids[i] == rClient.clientGuid)
					{
						relinkEntries.push_back({rPlayers.pGlobalPlayerIds[i], rCoord});
					}
				}
			}

			std::ranges::sort(relinkEntries, [](const RelinkEntry& rLeft, const RelinkEntry& rRight)
			{
				return rLeft.globalId.iValue < rRight.globalId.iValue;
			});

			rNewClientOwnedIds.reserve(relinkEntries.size());
			rClient.authorizedCoords.reserve(relinkEntries.size());
			for (const RelinkEntry& rEntry : relinkEntries)
			{
				rNewClientOwnedIds.push_back(rEntry.globalId);
				rClient.authorizedCoords.push_back(rEntry.coord);
				gpServerSession->SendAssignPlayer(rClient.iClientId, rEntry.globalId, rEntry.coord);
				gpServerSession->SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), rEntry.globalId.iValue, rEntry.coord);
				LOG(kNetwork, kVerbose, "ServerClientManager::NewClients Re-linked Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, rEntry.globalId, rEntry.coord.x, rEntry.coord.y);
			}

			if (!rNewClientOwnedIds.empty())
			{
				gpServerSession->mpFleetManager->OnClientConnected(rClient.iClientId, rClient.clientGuid);
				continue;
			}
		}

		// Client connects with zero players — spawns happen via fleet creation requests
		mProcessedClientIds.insert(rClient.iClientId);
		LOG(kNetwork, kVerbose, "ServerClientManager::NewClients Client: {} connected with no players", rClient.iClientId);
	}
}

void ServerClientManager::FinalizeNewClients([[maybe_unused]] int64_t iTick)
{
	if (mClientsWaitingForSpawn.empty())
	{
		return;
	}

	// Heap: vector operations
	ScopedSuppressAllocationTracking suppressAllocationTracking;

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
			gpServerSession->SendPlayerState(iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), globalPlayerId.iValue, engine::kOriginCoord);

			// Write client GUID into the player entity for save/load re-linking
			if (pClient != nullptr)
			{
				rPlayersPostRender.pClientGuids[iPlayerIndex] = pClient->clientGuid;
				gpServerSession->mClientOwnedPlayerIds.try_emplace(pClient->iClientId).first->second.push_back(globalPlayerId);
				pClient->authorizedCoords.push_back(engine::kOriginCoord);

				// Associate with fleet if this spawn was fleet-triggered
				const ClientSpawnInfo& rSpawnInfo = mClientsWaitingForSpawn.at(i);
				gpServerSession->mpFleetManager->OnPlayerSpawned(iClientId, rSpawnInfo, globalPlayerId);
			}
		}
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(uiAssignCount));

	// Refresh snapshot for subsequent ticks
	RefreshPreSpawnSnapshot();
}

void ServerClientManager::Disconnects()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingDisconnect& rDisconnect : engine::gpServer->DrainPendingDisconnects())
	{
		LOG(kNetwork, kVerbose, "ServerClientManager::Disconnects Client: {} Players: {}", rDisconnect.iClientId, gpServerSession->mClientOwnedPlayerIds.try_emplace(rDisconnect.iClientId).first->second.size());
		mDeadClientIds.erase(rDisconnect.iClientId);
		mProcessedClientIds.erase(rDisconnect.iClientId);

		gpServerSession->mpFleetManager->OnClientDisconnected(rDisconnect.iClientId, rDisconnect.clientGuid);

		// Remove from spawn queue if waiting
		std::erase_if(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
		{
			return rInfo.iClientId == rDisconnect.iClientId;
		});

		gpServerSession->mClientOwnedPlayerIds.erase(rDisconnect.iClientId);
	}

	// Unpause when no clients remain so the server resumes ticking for the next connection
	if (gpGame->mGameFlags & engine::GameFlags::kPaused && engine::gpServer->GetClients().empty())
	{
		gpGame->mGameFlags.Clear(engine::GameFlags::kPaused);
	}
}

void ServerClientManager::DetectPlayerDeaths()
{
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		std::vector<engine::global_id_t>& rDeathOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(rClient.iClientId).first->second;

		if (rDeathOwnedIds.empty())
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

		ScopedSuppressAllocationTracking suppressAllocationTracking;

		// Check each owned player for death (reverse iterate for safe removal)
		for (int64_t i = std::ssize(rDeathOwnedIds) - 1; i >= 0; --i)
		{
			engine::global_id_t globalId = rDeathOwnedIds.at(i);
			engine::GridCoord coord = rClient.authorizedCoords.at(i);

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
				gpServerSession->SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kDied), globalId.iValue, coord);
				LOG(kNetwork, kVerbose, "ServerClientManager::DetectPlayerDeaths Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId, coord.x, coord.y);
				rDeathOwnedIds.erase(rDeathOwnedIds.begin() + i);
				rClient.authorizedCoords.erase(rClient.authorizedCoords.begin() + i);

				gpServerSession->mpFleetManager->OnPlayerDeath(rClient.clientGuid, globalId);
			}
		}

		// Mark client as dead only when ALL owned players are dead
		if (rDeathOwnedIds.empty())
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
	mPendingPlayerDestroys.clear();
	mClientsWaitingForSpawn.clear();
	mDeadClientIds.clear();
	mProcessedClientIds.clear();
	mPreSpawnPlayerIds.clear();
}

#endif // BT_SERVER

} // namespace game
