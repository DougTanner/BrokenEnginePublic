#include "Pch.h"

#include "Network/ServerSession.h"

#include "Game.h"

#include "Network/PlayerEvents.h"
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

void ServerSession::PrepareTick()
{
	// During replay, PrepareActiveSet already configured mActiveCoords from the reader map
	if (gpGame->mGameSaveLoad.IsReplaying()) [[unlikely]]
	{
		return;
	}

	// Recompute active set each tick so new client subscriptions
	// (set by FinalizeNewClients on the previous frame) are picked up immediately
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ComputeActiveSet();
	gpGame->EnsureNextFrames();

	// Add empty frame inputs for any newly active coords
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		if (!gpGame->mFrameInputs.contains(rCoord))
		{
			gpGame->mFrameInputs.try_emplace(rCoord);
		}
	}
}

void ServerSession::BroadcastTick(int64_t iTick)
{
	// Heap: SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	HandleResyncRequests(iTick);
	FinalizeNewClients(iTick);
	DetectPlayerDeaths();
	BroadcastStatusChanges(iTick);
	mTickBroadcast.spawns.clear();
	SubscriptionUpdates(iTick);
	engine::gpServer->Flush();
}

void ServerSession::SendResends(int64_t iTick)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		engine::gpServer->SendResends(rClient, iTick);
	}
}

void ServerSession::WaitForTick(engine::TimeStep& rTimeStep)
{
	std::chrono::nanoseconds scaledTickNs = (kTickNs * rTimeStep.miTimeDivide) / rTimeStep.miTimeMultiply;
	ServerSessionBase::WaitForTick(rTimeStep, scaledTickNs);
}

void ServerSession::PreTickNetwork()
{
	// Heap: ENet polling and game server methods allocate vectors for inputs, spawns, and status changes
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	PollNetworkBase();
	Disconnects();
	NewClients();
	ProcessSpawnRequests();
}

void ServerSession::AddSubscribedCoords()
{
	const std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (int64_t i = 0; i < std::ssize(rClient.coordSubscriptions); ++i)
		{
			if (rClient.coordSubscriptions.at(i).bActive)
			{
				engine::GridCoord coord = rClient.coordSubscriptions.at(i).coord;
				if (!std::ranges::contains(gpGame->mActiveCoords, coord))
				{
					gpGame->mActiveCoords.push_back(coord);
				}
			}
		}
	}
}

void ServerSession::EnsurePlayerCoords()
{
	for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
	{
		if (rFrames.pCurrent->postRender.pPlayers->iCount > 0)
		{
			if (!std::ranges::contains(gpGame->mActiveCoords, rCoord))
			{
				gpGame->mActiveCoords.push_back(rCoord);
			}
		}
	}
}

void ServerSession::EnsureDestroyCoords()
{
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		if (!std::ranges::contains(gpGame->mActiveCoords, rDestroy.coord))
		{
			gpGame->mActiveCoords.push_back(rDestroy.coord);
		}
	}
}

void ServerSession::SyncActiveFrames()
{
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

void ServerSession::ComputeActiveSet()
{
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	gpGame->mActiveCoords.clear();
	AddSubscribedCoords();
	EnsurePlayerCoords();
	EnsureDestroyCoords();

	if (!std::ranges::contains(gpGame->mActiveCoords, engine::kOriginCoord))
	{
		gpGame->mActiveCoords.push_back(engine::kOriginCoord);
	}

	SyncActiveFrames();
}

void ServerSession::BuildFrameInputs()
{
	// Heap: unordered_map clear/insert, vector resize for statusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	gpGame->mFrameInputs.clear();
	mTickBroadcast.spawns.clear();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		gpGame->mFrameInputs.try_emplace(rCoord);
	}

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : mClientsWaitingForSpawn)
	{
		int64_t iGlobalId = gpGame->GenerateGlobalId();
		StatusChange spawnChange {.eType = StatusChangeType::kSpawnPlayer, .data = SpawnPlayerData{.iGlobalId = iGlobalId}};
		gpGame->mFrameInputs.try_emplace(rInfo.spawnCoord).first->second.statusChanges.push_back(spawnChange);
		Log(kLogNetwork, kVerbose, "BuildFrameInputs kSpawnPlayer Client: {} GlobalId: {} Coord: ({},{})", rInfo.iClientId, iGlobalId, rInfo.spawnCoord.x, rInfo.spawnCoord.y); // DT TEMP
	}

	// Add destroy StatusChanges for disconnected players
	for (const PendingPlayerDestroy& rDestroy : mPendingPlayerDestroys)
	{
		auto frameInputIt = gpGame->mFrameInputs.find(rDestroy.coord);
		if (frameInputIt == gpGame->mFrameInputs.end())
		{
			continue;
		}

		int64_t iPlayerUuid = rDestroy.playerId.ToUuid().Value();
		StatusChange destroyChange {.eType = StatusChangeType::kDestroyPlayer, .data = DestroyPlayerData{.iPlayerUuid = iPlayerUuid}};
		frameInputIt->second.statusChanges.push_back(destroyChange);
	}
	mPendingPlayerDestroys.clear();

	// Inject weapon mode toggle StatusChanges
	ProcessUpdatePlayerRequests();

	// Save StatusChanges for broadcasting (spawns only, transfers handled separately in HarvestTransfers)
	for (const auto& [rCoord, rFrameInput] : gpGame->mFrameInputs)
	{
		if (!rFrameInput.statusChanges.empty())
		{
			mTickBroadcast.spawns.insert_or_assign(rCoord, rFrameInput.statusChanges);
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

void ServerSession::BroadcastStatusChanges(int64_t iTick)
{
	// Heap: vector construction for grid updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Build unfiltered data: spawns + all transfers
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> allChanges;
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		auto spawnIt = mTickBroadcast.spawns.find(rCoord);
		if (spawnIt != mTickBroadcast.spawns.end())
		{
			allChanges.insert_or_assign(rCoord, spawnIt->second);
		}

		auto transferIt = mTickBroadcast.transfers.find(rCoord);
		if (transferIt != mTickBroadcast.transfers.end())
		{
			for (const StatusChange& rTransfer : transferIt->second)
			{
				allChanges.try_emplace(rCoord).first->second.push_back(rTransfer);
			}
		}
	}

	// Buffer per-coord frame data into ring buffers
	std::vector<std::pair<engine::GridCoord, engine::GridUpdateData>> allGridUpdates;
	allGridUpdates.reserve(gpGame->mActiveCoords.size());
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		engine::GridUpdateData updateData {};
		updateData.sharedCrc = gpGame->CurrentFrame(rCoord).postRender.sharedCrc;

		auto it = allChanges.find(rCoord);
		if (it != allChanges.end())
		{
			updateData.statusChanges = std::span<const StatusChange>(it->second);
		}

		updateData.inputCrc = gpGame->CurrentFrame(rCoord).postRender.previousInputCrc;

		allGridUpdates.push_back({rCoord, updateData});

		if (!updateData.statusChanges.empty())
		{
			Log(kLogNetwork, kVerbose, "BroadcastStatusChanges Coord: ({},{}) Tick: {} StatusChanges: {}", rCoord.x, rCoord.y, iTick, updateData.statusChanges.size());
		}
	}
	engine::gpServer->BufferFrame(iTick, allGridUpdates);

	// Buffer full frame snapshots for debug frame requests
	{
		std::vector<std::pair<engine::GridCoord, const game::Frame*>> fullFrames;
		fullFrames.reserve(gpGame->mActiveCoords.size());
		for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
		{
			fullFrames.push_back({rCoord, &gpGame->CurrentFrame(rCoord)});
		}
		engine::gpServer->BufferFullFrame(iTick, fullFrames);
	}

	// Send per-client updates (server iterates each client's subscribed slots internally)
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		engine::gpServer->SendUpdate(rClient, iTick);
	}
}

struct ClientTransferInfo
{
	engine::global_player_t globalPlayerId {};
	engine::GridCoord destination {};
	engine::ClientGuid clientGuid {};
};

static engine::ClientGuid TransferDataClientGuid(const TransferData& rData)
{
	return {rData.uiClientGuidHigh, rData.uiClientGuidLow};
}

void ServerSession::CollectTransfers(std::vector<ClientTransferInfo>& rClientTransfers)
{
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		Frame& rNextFrame = gpGame->NextFrame(rCoord);
		if (rNextFrame.postRender.transferRequests.empty())
		{
			continue;
		}

		for (const TransferRequest& rRequest : rNextFrame.postRender.transferRequests)
		{
			engine::GridCoord destination {rCoord.x + rRequest.iDeltaX, rCoord.y + rRequest.iDeltaY};

			auto it = gpGame->mCoordFrames.find(destination);
			if (it == gpGame->mCoordFrames.end() || it->second.pNext == nullptr)
			{
				gpGame->CreateFrameAtCoord(destination);
				auto& rFrames = gpGame->mCoordFrames.at(destination);
				rFrames.pNext = std::make_unique<Frame>();
				std::swap(rFrames.pCurrent, rFrames.pNext);
				it = gpGame->mCoordFrames.find(destination);
			}

			mTickBroadcast.transfers.try_emplace(destination).first->second.push_back({
				.eType = rRequest.eType,
				.data = StatusChangeData{rRequest.data},
			});

			if (rRequest.eType == StatusChangeType::kTransferPlayer && rRequest.data.globalPlayerId.IsValid())
			{
				rClientTransfers.push_back({
					.globalPlayerId = rRequest.data.globalPlayerId,
					.destination = destination,
					.clientGuid = TransferDataClientGuid(rRequest.data),
				});
			}
		}
	}
}

void ServerSession::SortTransfersByType()
{
	for (auto& [rCoord, rTransfers] : mTickBroadcast.transfers)
	{
		std::ranges::sort(rTransfers, [](const StatusChange& rLeft, const StatusChange& rRight)
		{
			return rLeft.eType < rRight.eType;
		});
	}
}

void ServerSession::SpawnTransfers()
{
	for (auto& [rCoord, rTransfers] : mTickBroadcast.transfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		for (const StatusChange& rTransfer : rTransfers)
		{
			TransferData data = std::get<TransferData>(rTransfer.data);
			SpawnTransfer(rDestFrame, rTransfer.eType, data, gpGame->PlayerAlignment());
		}
	}
}

void ServerSession::TrackClientTransfers(const std::vector<ClientTransferInfo>& rClientTransfers)
{
	for (const ClientTransferInfo& rClientTransfer : rClientTransfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rClientTransfer.destination).pNext;
		PlayersPostRender& rDestPlayers = *rDestFrame.postRender.pPlayers;

		// Find the transferred player in destination by scanning pGlobalPlayerIds
		int64_t iNewIndex = -1;
		for (int64_t i = 0; i < rDestPlayers.iCount; ++i)
		{
			if (rDestPlayers.pGlobalPlayerIds[i] == rClientTransfer.globalPlayerId)
			{
				iNewIndex = i;
				break;
			}
		}
		if (iNewIndex < 0)
		{
			continue;
		}

		bool bFoundClient = false;
		std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
		for (engine::ClientConnection& rClient : rClients)
		{
			// Find the client that owns this global ID
			for (int64_t k = 0; k < std::ssize(rClient.ownedPlayerIds); ++k)
			{
				if (rClient.ownedPlayerIds.at(k) == rClientTransfer.globalPlayerId)
				{
					rClient.ownedPlayerCoords.at(k) = rClientTransfer.destination;
					mPendingSubscriptionUpdates.push_back({
						.iClientId = rClient.iClientId,
						.newCoord = rClientTransfer.destination,
						.globalPlayerId = rClientTransfer.globalPlayerId,
					});

					// Copy client GUID to the new player entity in the destination frame
					rDestPlayers.pClientGuids[iNewIndex] = rClient.clientGuid;
					bFoundClient = true;
					break;
				}
			}
			if (bFoundClient)
			{
				break;
			}
		}

		// Preserve GUID for orphaned players (client disconnected)
		if (!bFoundClient && !rClientTransfer.clientGuid.IsEmpty())
		{
			rDestPlayers.pClientGuids[iNewIndex] = rClientTransfer.clientGuid;
		}
	}
}

void ServerSession::HarvestTransfers()
{
	// Heap: Transfer spawns into destination frames, which may grow SOA buffers and update idToIndexMaps
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mTickBroadcast.transfers.clear();
	std::vector<ClientTransferInfo> clientTransfers;

	CollectTransfers(clientTransfers);
	SortTransfersByType();
	SpawnTransfers();

	// Recompute CRCs for destination frames after transfers modified them
	// (RunFrameTick computed CRCs before HarvestTransfers spawned entities)
	for (const auto& [rCoord, rTransfers] : mTickBroadcast.transfers)
	{
		Frame& rDestFrame = *gpGame->mCoordFrames.at(rCoord).pNext;
		rDestFrame.postRender.sharedCrc = rDestFrame.Crcs();
	}

	TrackClientTransfers(clientTransfers);
}

void ServerSession::ProcessSpawnRequests()
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
			if (!std::ranges::contains(mClientsWaitingForSpawn, rRequest.iClientId, &ClientSpawnInfo::iClientId))
			{
				mClientsWaitingForSpawn.push_back({rRequest.iClientId, engine::kOriginCoord});
			}
		}
	}
}

void ServerSession::ProcessUpdatePlayerRequests()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingUpdatePlayerRequest& rRequest : engine::gpServer->DrainPendingUpdatePlayerRequests())
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr || pClient->ownedPlayerIds.empty())
		{
			continue;
		}

		// Find the coord for this global player ID in the client's owned list
		engine::GridCoord updateCoord {};
		bool bFound = false;
		for (int64_t i = 0; i < std::ssize(pClient->ownedPlayerIds); ++i)
		{
			if (pClient->ownedPlayerIds.at(i) == rRequest.globalPlayerId)
			{
				updateCoord = pClient->ownedPlayerCoords.at(i);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			continue;
		}

		auto frameInputIt = gpGame->mFrameInputs.find(updateCoord);
		if (frameInputIt == gpGame->mFrameInputs.end())
		{
			continue;
		}

		// Find the frame-local player ID by scanning pGlobalPlayerIds
		if (!gpGame->mCoordFrames.contains(updateCoord))
		{
			continue;
		}
		const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(updateCoord).postRender.pPlayers;
		int64_t iPlayerUuid = 0;
		for (int64_t j = 0; j < rPlayers.iCount; ++j)
		{
			if (rPlayers.pGlobalPlayerIds[j] == rRequest.globalPlayerId)
			{
				iPlayerUuid = rPlayers.puiIds[j].ToUuid().Value();
				break;
			}
		}
		if (iPlayerUuid == 0)
		{
			continue;
		}

		StatusChange updateChange {.eType = StatusChangeType::kUpdatePlayer, .data = UpdatePlayerData{.iPlayerUuid = iPlayerUuid, .bUseMissiles = rRequest.bUseMissiles, .fNavigationDelay = rRequest.fNavigationDelay}};
		frameInputIt->second.statusChanges.push_back(updateChange);

		Log(kLogNetwork, kVerbose, "ServerSession::ProcessUpdatePlayerRequests Client: {} GlobalPlayer: {} Coord: ({},{})", rRequest.iClientId, rRequest.globalPlayerId.iValue, updateCoord.x, updateCoord.y);
	}
}

void ServerSession::NewClients()
{
	// Heap: vector push_back for waiting clients
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		if (!rClient.ownedPlayerIds.empty())
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

		// Re-link with existing players by matching ClientGuid (sorted by global ID to preserve creation order)
		if (!rClient.clientGuid.IsEmpty())
		{
			struct RelinkEntry
			{
				engine::global_player_t globalId {};
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

			rClient.ownedPlayerIds.reserve(relinkEntries.size());
			rClient.ownedPlayerCoords.reserve(relinkEntries.size());
			for (const RelinkEntry& rEntry : relinkEntries)
			{
				rClient.ownedPlayerIds.push_back(rEntry.globalId);
				rClient.ownedPlayerCoords.push_back(rEntry.coord);
				engine::gpServer->SendAssignPlayer(rClient.iClientId, rEntry.globalId, rEntry.coord);
				engine::gpServer->SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), rEntry.globalId.iValue, rEntry.coord);
				Log(kLogNetwork, kVerbose, "NewClients Re-linked Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, rEntry.globalId.iValue, rEntry.coord.x, rEntry.coord.y); // DT TEMP
			}

			if (!rClient.ownedPlayerIds.empty())
			{
				continue;
			}
		}

		mClientsWaitingForSpawn.push_back({rClient.iClientId, engine::kOriginCoord});
		Log(kLogNetwork, "NewClients Added Client: {} Handshake: {}", rClient.iClientId, rClient.bHandshakeComplete); // DT TEMP
	}
}

void ServerSession::RefreshPreSpawnSnapshot()
{
	mPreSpawnPlayerIds.clear();
	const PlayersPostRender& rPlayers = *gpGame->CurrentFrame(engine::kOriginCoord).postRender.pPlayers;
	mPreSpawnPlayerIds.reserve(static_cast<size_t>(rPlayers.iCount));
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

	Log(kLogNetwork, kVerbose, "FinalizeNewClients Waiting: {} PlayerCount: {} PreSpawn: {} NewIds: {}", mClientsWaitingForSpawn.size(), rPlayers.iCount, mPreSpawnPlayerIds.size(), newPlayerIds.size()); // DT TEMP

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
			engine::global_player_t globalPlayerId = rPlayersPostRender.pGlobalPlayerIds[iPlayerIndex];

			engine::gpServer->SendAssignPlayer(iClientId, globalPlayerId, engine::kOriginCoord);
			engine::gpServer->SendPlayerState(iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), globalPlayerId.iValue, engine::kOriginCoord);

			// Write client GUID into the player entity for save/load re-linking
			if (pClient != nullptr)
			{
				rPlayersPostRender.pClientGuids[iPlayerIndex] = pClient->clientGuid;
				pClient->ownedPlayerIds.push_back(globalPlayerId);
				pClient->ownedPlayerCoords.push_back(engine::kOriginCoord);
			}
		}
	}

	mClientsWaitingForSpawn.erase(mClientsWaitingForSpawn.begin(), mClientsWaitingForSpawn.begin() + static_cast<int64_t>(uiAssignCount));

	// Refresh snapshot for subsequent ticks
	RefreshPreSpawnSnapshot();
}

void ServerSession::Disconnects()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const engine::PendingDisconnect& rDisconnect : engine::gpServer->DrainPendingDisconnects())
	{
		Log(kLogNetwork, kVerbose, "ServerSession::Disconnects Client: {} Players: {}", rDisconnect.iClientId, rDisconnect.playerIds.size()); // DT TEMP

		mDeadClientIds.erase(rDisconnect.iClientId);

		// Remove from spawn queue if waiting
		std::erase_if(mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rInfo)
		{
			return rInfo.iClientId == rDisconnect.iClientId;
		});
	}

	// Unpause when no clients remain so the server resumes ticking for the next connection
	if (gpGame->mGameFlags & engine::GameFlags::kPaused && engine::gpServer->GetClients().empty())
	{
		gpGame->mGameFlags.Clear(engine::GameFlags::kPaused);
	}
}

void ServerSession::DetectPlayerDeaths()
{
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		if (rClient.ownedPlayerIds.empty())
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

		ScopedSuppressAllocationTracking suppressAllocationTracking;

		// Check each owned player for death (reverse iterate for safe removal)
		for (int64_t i = std::ssize(rClient.ownedPlayerIds) - 1; i >= 0; --i)
		{
			engine::global_player_t globalId = rClient.ownedPlayerIds.at(i);
			engine::GridCoord coord = rClient.ownedPlayerCoords.at(i);

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
				engine::gpServer->SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kDied), globalId.iValue, coord);
				Log(kLogNetwork, kVerbose, "ServerSession::DetectPlayerDeaths Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId.iValue, coord.x, coord.y);
				rClient.ownedPlayerIds.erase(rClient.ownedPlayerIds.begin() + i);
				rClient.ownedPlayerCoords.erase(rClient.ownedPlayerCoords.begin() + i);
			}
		}

		// Mark client as dead only when ALL owned players are dead
		if (rClient.ownedPlayerIds.empty())
		{
			mDeadClientIds.insert(rClient.iClientId);
		}
	}
}

void ServerSession::SubscriptionUpdates([[maybe_unused]] int64_t iTick)
{
	SendNewSubscriptionFullStates(iTick);

	if (mPendingSubscriptionUpdates.empty())
	{
		return;
	}

	// Client handles subscriptions — server just sends player assignment with global ID
	for (const SubscriptionUpdate& rUpdate : mPendingSubscriptionUpdates)
	{
		engine::gpServer->SendAssignPlayer(rUpdate.iClientId, rUpdate.globalPlayerId, rUpdate.newCoord);
		engine::gpServer->SendPlayerState(rUpdate.iClientId, PlayerEventTypeToWire(PlayerEventType::kChangedFrame), rUpdate.globalPlayerId.iValue, rUpdate.newCoord);
	}

	mPendingSubscriptionUpdates.clear();
}

void ServerSession::HandleResyncRequests([[maybe_unused]] int64_t iTick)
{
	std::vector<int64_t>& rResyncClientIds = engine::gpServer->DrainPendingResyncClientIds();
	if (rResyncClientIds.empty())
	{
		return;
	}

	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (int64_t iClientId : rResyncClientIds)
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		Log(kLogNetwork, kWarning, "ServerSession::HandleResyncRequests Client: {}", iClientId);

		for (int64_t iSlot = 0; iSlot < std::ssize(pClient->coordSubscriptions); ++iSlot)
		{
			if (!pClient->coordSubscriptions.at(iSlot).bActive)
			{
				continue;
			}

			engine::GridCoord coord = pClient->coordSubscriptions.at(iSlot).coord;
			auto frameIt = gpGame->mCoordFrames.find(coord);
			if (frameIt == gpGame->mCoordFrames.end())
			{
				continue;
			}

			engine::gpServer->SendCoordFullState(iClientId, iSlot, gpGame->TickCounter(), coord, frameIt->second.pCurrent.get());
		}
	}
}

void ServerSession::ResetClientsForLoad()
{
	Log("ServerSession::ResetClientsForLoad");
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	engine::gpServer->BroadcastLoadNotification();

	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();

	// Try to re-link each client to their players by GUID
	for (engine::ClientConnection& rClient : rClients)
	{
		// Free all subscription slots
		for (int64_t i = 0; i < std::ssize(rClient.coordSubscriptions); ++i)
		{
			if (rClient.coordSubscriptions.at(i).bActive)
			{
				rClient.FreeSlot(i);
			}
		}

		// Clear owned vectors and rebuild from loaded frames
		rClient.ownedPlayerIds.clear();
		rClient.ownedPlayerCoords.clear();

		if (!rClient.clientGuid.IsEmpty())
		{
			for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
			{
				const PlayersPostRender& rPlayers = *rFrames.pCurrent->postRender.pPlayers;
				for (int64_t i = 0; i < rPlayers.iCount; ++i)
				{
					if (rPlayers.pClientGuids[i] == rClient.clientGuid)
					{
						engine::global_player_t globalId = rPlayers.pGlobalPlayerIds[i];
						rClient.ownedPlayerIds.push_back(globalId);
						rClient.ownedPlayerCoords.push_back(rCoord);

						engine::gpServer->SendAssignPlayer(rClient.iClientId, globalId, rCoord);
						engine::gpServer->SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), globalId.iValue, rCoord);
						Log("ResetClientsForLoad Client: {} re-linked to GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId.iValue, rCoord.x, rCoord.y);
					}
				}
			}
		}

		if (rClient.ownedPlayerIds.empty())
		{
			Log("ResetClientsForLoad Client: {} no GUID match, will respawn", rClient.iClientId);
		}
	}

	// Clear all pending server session state
	mPendingPlayerDestroys.clear();
	mClientsWaitingForSpawn.clear();
	mDeadClientIds.clear();
	mPendingSubscriptionUpdates.clear();
	mTickBroadcast.spawns.clear();
	mTickBroadcast.transfers.clear();
	mPreSpawnPlayerIds.clear();

	// Clear stale ring buffers and pending events
	engine::gpServer->ClearBufferedFrames();
	engine::gpServer->DrainPendingSpawnRequests().clear();
	engine::gpServer->DrainPendingNewSubscriptions().clear();
	engine::gpServer->DrainPendingResyncClientIds().clear();
	engine::gpServer->DrainPendingUpdatePlayerRequests().clear();

	engine::gpServer->Flush();
}

#endif // BT_SERVER

} // namespace game
