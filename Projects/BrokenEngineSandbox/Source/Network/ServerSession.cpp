#include "Game.h"

#include "Network/ServerSession.h"
#include "Frame/Collections/Players/Players.h"

namespace game
{

#if defined(BT_SERVER)

ServerSession::ServerSession()
{
	gpServerSession = this;
}

ServerSession::~ServerSession()
{
	gpServerSession = nullptr;
}

void ServerSession::PreTickNetwork()
{
	// Heap: ENet polling and game server methods allocate vectors for inputs, spawns, and status changes
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	engine::gpNetworkServer->Poll();
	mpDiscoveryResponder->Poll();
	HandleDisconnects();
	HandleNewClients();
	ProcessSpawnRequests();
}

void ServerSession::HandleDisconnects()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingDisconnect& rDisconnect : engine::gpNetworkServer->DrainPendingDisconnects())
	{
		// Queue player destruction for the next tick
		if (rDisconnect.playerId.IsValid())
		{
			mPendingPlayerDestroys.push_back({.coord = rDisconnect.coord, .playerId = rDisconnect.playerId});
		}

		mDeadClientIds.erase(rDisconnect.iClientId);

		// Remove from spawn queue if waiting
		std::erase_if(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
		{
			return rInfo.iClientId == rDisconnect.iClientId;
		});
	}
}

void ServerSession::ComputeActiveSet()
{
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	gpGame->mActiveCoords.clear();

	// Union all clients' subscribed coords
	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (int64_t i = 0; i < std::ssize(rClient.coordSubscriptions); ++i)
		{
			if (rClient.coordSubscriptions[i].bActive)
			{
				engine::GridCoord coord = rClient.coordSubscriptions[i].coord;
				if (!std::ranges::contains(gpGame->mActiveCoords, coord))
				{
					gpGame->mActiveCoords.push_back(coord);
				}
			}
		}
	}

	// Add 8 neighbours around each player's Frame so transfers have a destination
	for (const engine::ClientConnection& rClient : rClients)
	{
		if (!rClient.humanPlayerId.IsValid())
		{
			continue;
		}

		for (int64_t i = -1; i <= 1; ++i)
		{
			for (int64_t j = -1; j <= 1; ++j)
			{
				if (j == 0 && i == 0)
				{
					continue;
				}

				engine::GridCoord neighbor {rClient.humanGridCoord.x + static_cast<int32_t>(j), rClient.humanGridCoord.y + static_cast<int32_t>(i)};
				if (!std::ranges::contains(gpGame->mActiveCoords, neighbor))
				{
					gpGame->mActiveCoords.push_back(neighbor);
				}
			}
		}
	}

	// Origin is always active
	if (!std::ranges::contains(gpGame->mActiveCoords, engine::kOriginCoord))
	{
		gpGame->mActiveCoords.push_back(engine::kOriginCoord);
	}

	// Ensure destroy coords are active so the StatusChange is processed and broadcast
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		if (!std::ranges::contains(gpGame->mActiveCoords, rDestroy.coord))
		{
			gpGame->mActiveCoords.push_back(rDestroy.coord);
		}
	}

	// Create frames at missing coordinates
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		if (!gpGame->mCoordFrames.contains(rCoord))
		{
			gpGame->CreateFrameAtCoord(rCoord);
		}
	}

	// Delete frames outside the active set
	std::erase_if(gpGame->mCoordFrames, [](const auto& rPair)
	{
		return !std::ranges::contains(gpGame->mActiveCoords, rPair.first);
	});
}

void ServerSession::BuildFrameInputs()
{
	// Heap: unordered_map clear/insert, vector resize for statusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	gpGame->mFrameInputs.clear();
	mBroadcastSpawns.clear();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		gpGame->mFrameInputs[rCoord];
	}

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
	{
		gpGame->mFrameInputs[rInfo.spawnCoord].statusChanges.push_back({.eType = StatusChangeType::kSpawnPlayer});
	}

	// Add destroy StatusChanges for disconnected players
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		auto frameInputIt = gpGame->mFrameInputs.find(rDestroy.coord);
		if (frameInputIt == gpGame->mFrameInputs.end())
		{
			continue;
		}

		StatusChange destroyChange {.eType = StatusChangeType::kDestroyPlayer};
		int64_t iPlayerUuid = rDestroy.playerId.ToUuid().Value();
		XMFLOAT4A f4 {};
		std::memcpy(&f4, &iPlayerUuid, sizeof(int64_t));
		destroyChange.data.vecPosition = XMLoadFloat4A(&f4);
		frameInputIt->second.statusChanges.push_back(destroyChange);
	}
	mPendingPlayerDestroys.clear();

	// Save StatusChanges for broadcasting (spawns only, transfers handled separately in HarvestTransfers)
	for (const auto& [rCoord, rFrameInput] : gpGame->mFrameInputs)
	{
		if (!rFrameInput.statusChanges.empty())
		{
			mBroadcastSpawns[rCoord] = rFrameInput.statusChanges;
		}
	}

	// Take snapshot of player IDs at spawn coordinates for FinalizeNewClients
	if (!mClientsWaitingForSpawn.empty() && gpGame->mCoordFrames.contains(engine::kOriginCoord))
	{
		RefreshPreSpawnSnapshot();
	}
	else
	{
		mPreSpawnPlayerIds.clear();
	}
}

void ServerSession::ProcessSpawnRequests()
{
	// Heap: vector push_back for spawn StatusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingSpawnRequest& rRequest : engine::gpNetworkServer->DrainPendingSpawnRequests())
	{
		const engine::ClientConnection* pClient = engine::gpNetworkServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		if (rRequest.flags & engine::ClientRequestFlags::kRespawnRequested ||
		    rRequest.flags & engine::ClientRequestFlags::kSpawnRequested)
		{
			mDeadClientIds.erase(rRequest.iClientId);
			mClientsWaitingForSpawn.push_back({rRequest.iClientId, engine::kOriginCoord});
		}
	}
}

void ServerSession::HarvestTransfers()
{
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mBroadcastTransfers.clear();

	// Collect human transfer info for subscription updates after spawning
	struct HumanTransferInfo
	{
		int64_t iEntityId;
		engine::GridCoord dest;
	};
	std::vector<HumanTransferInfo> humanTransfers;

	// Phase 1: Collect transfers into mBroadcastTransfers (no spawning yet)
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		Frame& rNextFrame = gpGame->NextFrame(rCoord);
		if (rNextFrame.postRender.transferRequests.empty())
		{
			continue;
		}

		for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
		{
			engine::GridCoord dest {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};

			auto it = gpGame->mCoordFrames.find(dest);
			if (it == gpGame->mCoordFrames.end() || it->second.pNext == nullptr)
			{
				continue;
			}

			mBroadcastTransfers[dest].push_back({.eType = rRequest.eType, .data = rRequest.data});

			if (rRequest.eType == StatusChangeType::kTransferPlayer && rRequest.iEntityId != 0)
			{
				humanTransfers.push_back({.iEntityId = rRequest.iEntityId, .dest = dest});
			}
		}
	}

	// Phase 2: Sort each destination's transfers by type (matches serialization's type-grouped order)
	for (auto& [rCoord, rTransfers] : mBroadcastTransfers)
	{
		std::ranges::sort(rTransfers, [](const StatusChange& rLeft, const StatusChange& rRight)
		{
			return rLeft.eType < rRight.eType;
		});
	}

	// Phase 3: Spawn in sorted order
	for (auto& [rCoord, rTransfers] : mBroadcastTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		for (const StatusChange& rTransfer : rTransfers)
		{
			TransferData data = rTransfer.data;
			SpawnTransfer(rDestFrame, rTransfer.eType, data, gpGame->PlayerAlignment());
		}
	}

	// Phase 4: Track human player transfers for subscription updates
	for (const HumanTransferInfo& rHumanTransfer : humanTransfers)
	{
		player_t transferredPlayerId {engine::uuid_t {rHumanTransfer.iEntityId}};
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rHumanTransfer.dest).pNext;
		player_t newPlayerId = rDestFrame.postRender.pPlayers->puiIds[rDestFrame.postRender.pPlayers->iCount - 1];

		const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
		for (const engine::ClientConnection& rClient : rClients)
		{
			if (rClient.humanPlayerId.IsValid() && transferredPlayerId == rClient.humanPlayerId)
			{
				mPendingSubscriptionUpdates.push_back({.iClientId = rClient.iClientId, .newCoord = rHumanTransfer.dest, .newPlayerId = newPlayerId});

				FILE_LOG(0, "[HarvestTransfers] Human transfer: client={} oldId={} newId={} dest=({},{})", rClient.iClientId, transferredPlayerId.ToUuid().Value(), newPlayerId.ToUuid().Value(), rHumanTransfer.dest.x, rHumanTransfer.dest.y);

				break;
			}
		}
	}
}

void ServerSession::BroadcastStatusChanges(int64_t iTick)
{
	// Heap: vector construction for grid updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Build unfiltered data: spawns + all transfers
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> allChanges;
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		auto spawnIt = mBroadcastSpawns.find(rCoord);
		if (spawnIt != mBroadcastSpawns.end())
		{
			allChanges[rCoord] = spawnIt->second;
		}

		auto transferIt = mBroadcastTransfers.find(rCoord);
		if (transferIt != mBroadcastTransfers.end())
		{
			for (const StatusChange& rTransfer : transferIt->second)
			{
				allChanges[rCoord].push_back(rTransfer);
			}
		}
	}

	// Buffer per-coord frame data into ring buffers
	std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> allGridUpdates;
	allGridUpdates.reserve(gpGame->mActiveCoords.size());
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		engine::GridUpdateData updateData {};
		updateData.serverCrc = gpGame->CurrentFrame(rCoord).postRender.serverCrc;

		auto it = allChanges.find(rCoord);
		if (it != allChanges.end())
		{
			updateData.statusChanges = std::span<const StatusChange>(it->second);
		}

		updateData.inputCrc = gpGame->CurrentFrame(rCoord).postRender.previousInputCrc;

		allGridUpdates.push_back({rCoord, updateData});
	}
	engine::gpNetworkServer->BufferFrame(iTick, allGridUpdates);
	common::Log("GameServer: BroadcastStatusChanges tick={} coords={} clients={}", iTick, gpGame->mActiveCoords.size(), engine::gpNetworkServer->GetClients().size()); // DT: TEMP

	// Buffer full frame snapshots for debug frame requests
	{
		std::vector<std::pair<engine::GridCoord, const game::Frame*>> fullFrames;
		fullFrames.reserve(gpGame->mActiveCoords.size());
		for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
		{
			fullFrames.push_back({rCoord, &gpGame->CurrentFrame(rCoord)});
		}
		engine::gpNetworkServer->BufferFullFrame(iTick, fullFrames);
	}

	// Send per-client updates (server iterates each client's subscribed slots internally)
	std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		engine::gpNetworkServer->SendUpdate(rClient, iTick);
		engine::gpNetworkServer->SendResends(rClient, iTick);
	}
}

void ServerSession::HandleNewClients()
{
	// Heap: vector push_back for waiting clients
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		if (rClient.humanPlayerId.IsValid())
		{
			continue;
		}

		if (mDeadClientIds.contains(rClient.iClientId))
		{
			continue;
		}

		if (std::ranges::contains(mClientsWaitingForSpawn, rClient.iClientId, &ClientSpawnInfo::iClientId))
		{
			continue;
		}

		mClientsWaitingForSpawn.push_back({rClient.iClientId, engine::kOriginCoord});
	}
}

void ServerSession::RefreshPreSpawnSnapshot()
{
	mPreSpawnPlayerIds.clear();
	const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	for (int64_t i = 0; i < rPlayers.iCount; ++i)
	{
		mPreSpawnPlayerIds.push_back(rPlayers.puiIds[i]);
	}
}

void ServerSession::FinalizeNewClients([[maybe_unused]] int64_t iTick)
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

	// Assign new players to waiting clients (in order)
	// Client handles subscriptions — no full state sent here
	size_t iAssignCount = std::min(mClientsWaitingForSpawn.size(), newPlayerIds.size());
	for (size_t i = 0; i < iAssignCount; ++i)
	{
		int64_t iClientId = mClientsWaitingForSpawn.at(i).iClientId;
		player_t playerId = newPlayerIds.at(i);

		engine::gpNetworkServer->SendAssignPlayer(iClientId, playerId, engine::kOriginCoord);
		engine::gpNetworkServer->SendPlayerState(iClientId, engine::PlayerStateType::kSpawned, playerId, engine::kOriginCoord);
		common::Log("GameServer: AssignPlayer client={} player={} coord ({},{})", iClientId, playerId.ToUuid().Value(), engine::kOriginCoord.x, engine::kOriginCoord.y); // DT: TEMP
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(iAssignCount));

	// Refresh snapshot for subsequent ticks
	RefreshPreSpawnSnapshot();
}

void ServerSession::DetectPlayerDeaths()
{
	std::vector<engine::ClientConnection>& rClients = engine::gpNetworkServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		if (!rClient.humanPlayerId.IsValid())
		{
			continue;
		}

		if (mDeadClientIds.contains(rClient.iClientId))
		{
			continue;
		}

		// Skip clients mid-transfer (subscription update pending from HarvestTransfers)
		if (std::ranges::contains(mPendingSubscriptionUpdates, rClient.iClientId, &SubscriptionUpdate::iClientId))
		{
			continue;
		}

		if (!gpGame->mCoordFrames.contains(rClient.humanGridCoord))
		{
			continue;
		}

		const PlayersInterpolate& rPlayers = *gpGame->CurrentFrame(rClient.humanGridCoord).interpolate.pPlayers;
		if (rPlayers.idToIndexMap.contains(rClient.humanPlayerId))
		{
			continue;
		}

		// Player not found in frame — they died
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		mDeadClientIds.insert(rClient.iClientId);
		engine::gpNetworkServer->SendPlayerState(rClient.iClientId, engine::PlayerStateType::kDied, rClient.humanPlayerId, rClient.humanGridCoord);
		common::Log("GameServer: PlayerDeath client={} player={} coord ({},{})", rClient.iClientId, rClient.humanPlayerId.ToUuid().Value(), rClient.humanGridCoord.x, rClient.humanGridCoord.y); // DT: TEMP
		rClient.humanPlayerId = {};
	}
}

void ServerSession::HandleSubscriptionUpdates([[maybe_unused]] int64_t iTick)
{
	// Send full state for newly subscribed coords (validate slot is still active)
	std::vector<engine::PendingNewSubscription>& rNewSubs = engine::gpNetworkServer->DrainPendingNewSubscriptions();
	for (const engine::PendingNewSubscription& rSub : rNewSubs)
	{
		const engine::ClientConnection* pClient = engine::gpNetworkServer->FindClient(rSub.iClientId);
		bool bSlotStillValid = (pClient != nullptr
			&& rSub.iSlot < std::ssize(pClient->coordSubscriptions)
			&& pClient->coordSubscriptions[rSub.iSlot].bActive
			&& pClient->coordSubscriptions[rSub.iSlot].coord == rSub.coord);
		if (!bSlotStillValid)
		{
			continue;
		}

		auto frameIt = gpGame->mCoordFrames.find(rSub.coord);
		if (frameIt != gpGame->mCoordFrames.end())
		{
			common::Log("GameServer: SendFullState client={} slot={} coord ({},{}) tick={}", rSub.iClientId, rSub.iSlot, rSub.coord.x, rSub.coord.y, gpGame->TickCounter()); // DT: TEMP
			engine::gpNetworkServer->SendCoordFullState(rSub.iClientId, rSub.iSlot, gpGame->TickCounter(), rSub.coord, frameIt->second.pCurrent.get());
		}
	}

	if (mPendingSubscriptionUpdates.empty())
	{
		return;
	}

	// Client handles subscriptions — server just sends player assignment
	for (const SubscriptionUpdate& rUpdate : mPendingSubscriptionUpdates)
	{
		engine::gpNetworkServer->SendAssignPlayer(rUpdate.iClientId, rUpdate.newPlayerId, rUpdate.newCoord);
		engine::gpNetworkServer->SendPlayerState(rUpdate.iClientId, engine::PlayerStateType::kChangedFrame, rUpdate.newPlayerId, rUpdate.newCoord);

		FILE_LOG(0, "[HandleSubscriptionUpdates] client={} newId={} newCoord=({},{})", rUpdate.iClientId, rUpdate.newPlayerId.ToUuid().Value(), rUpdate.newCoord.x, rUpdate.newCoord.y);
	}

	mPendingSubscriptionUpdates.clear();
}

#endif // BT_SERVER

} // namespace game
