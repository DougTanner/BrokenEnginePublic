#include "Pch.h"

#include "Network/Server/ServerBroadcaster.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerSession.h"
#include "Network/Server/ServerTransferManager.h"

namespace game
{

#if defined(BT_SERVER)

void ServerBroadcaster::BuildFrameInputs()
{
	// Heap: unordered_map clear/insert, vector resize for statusChanges
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	gpGame->mFrameInputs.clear();
	mSpawns.clear();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		gpGame->mFrameInputs.try_emplace(rCoord);
	}

	// Add spawn StatusChanges for clients waiting for initial spawn
	for (const ClientSpawnInfo& rInfo : gpServerSession->mpClientManager->mClientsWaitingForSpawn)
	{
		int64_t iGlobalId = gpGame->GenerateGlobalId();

		bool bIsFlagship = false;
		engine::GridCoord spawnFleetWantedCoord {};
		uint8_t spawnPendingFleetTicks = 0;
		if (rInfo.iFleetIndex >= 0)
		{
			ServerFleetManager::FleetLookupResult result = gpServerSession->mpFleetManager->LookupFleetWantedCoord(rInfo.iClientId, rInfo.iFleetIndex, rInfo.iMemberIndex, rInfo.spawnCoord);
			bIsFlagship = result.bIsFlagship;
			spawnFleetWantedCoord = result.fleetWantedCoord;
			spawnPendingFleetTicks = result.uiPendingFleetWantedCoordTicks;
		}

		StatusChange spawnChange {.eType = StatusChangeType::kSpawnPlayer, .data = SpawnPlayerData{.iGlobalId = iGlobalId, .bIsFlagship = bIsFlagship, .fleetWantedCoord = spawnFleetWantedCoord, .uiPendingFleetWantedCoordTicks = spawnPendingFleetTicks}};
		gpGame->mFrameInputs.try_emplace(rInfo.spawnCoord).first->second.statusChanges.push_back(spawnChange);
		LOG(kNetwork, kVerbose, "ServerBroadcaster::BuildFrameInputs::kSpawnPlayer Client: {} GlobalId: {} Coord: ({},{}) Flagship: {}", rInfo.iClientId, iGlobalId, rInfo.spawnCoord.x, rInfo.spawnCoord.y, bIsFlagship);
	}

	// Add destroy StatusChanges for disconnected players
	for (const PendingPlayerDestroy& rDestroy : gpServerSession->mpClientManager->mPendingPlayerDestroys)
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
	gpServerSession->mpClientManager->mPendingPlayerDestroys.clear();

	// Inject weapon mode toggle StatusChanges
	ProcessUpdatePlayerRequests();

	// Tick fleet timers and inject fleet coord updates
	gpServerSession->mpFleetManager->TickFleetTimers();
	gpServerSession->mpFleetManager->ProcessFlagshipUpdates();

	// Save StatusChanges for broadcasting (spawns only, transfers handled separately in HarvestTransfers)
	for (const auto& [rCoord, rFrameInput] : gpGame->mFrameInputs)
	{
		if (!rFrameInput.statusChanges.empty())
		{
			mSpawns.insert_or_assign(rCoord, rFrameInput.statusChanges);
		}
	}

	// Take snapshot of player IDs at spawn coordinates for FinalizeNewClients
	if (!gpServerSession->mpClientManager->mClientsWaitingForSpawn.empty() && gpGame->mCoordFrames.contains(engine::kOriginCoord))
	{
		gpServerSession->mpClientManager->RefreshPreSpawnSnapshot();
	}
	else
	{
		gpServerSession->mpClientManager->mPreSpawnPlayerIds.clear();
	}
}

void ServerBroadcaster::BroadcastStatusChanges(int64_t iTick)
{
	// Heap: vector construction for grid updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::unordered_map<engine::GridCoord, std::vector<StatusChange>>& rTransfers = gpServerSession->mpTransferManager->mTransfers;

	// Build unfiltered data: spawns + all transfers
	std::unordered_map<engine::GridCoord, std::vector<StatusChange>> allChanges;
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		auto spawnIt = mSpawns.find(rCoord);
		if (spawnIt != mSpawns.end())
		{
			allChanges.insert_or_assign(rCoord, spawnIt->second);
		}

		auto transferIt = rTransfers.find(rCoord);
		if (transferIt != rTransfers.end())
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

		allGridUpdates.push_back({rCoord, updateData});

		if (!updateData.statusChanges.empty())
		{
			LOG(kNetwork, kVerbose, "ServerBroadcaster::BroadcastStatusChanges Coord: ({},{}) Tick: {} StatusChanges: {}", rCoord.x, rCoord.y, iTick, updateData.statusChanges.size());
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

void ServerBroadcaster::ProcessUpdatePlayerRequests()
{
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	for (const PendingUpdatePlayerRequest& rRequest : mPendingUpdatePlayerRequests)
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}
		std::vector<engine::global_id_t>& rOwnedIds = gpServerSession->mClientOwnedPlayerIds.try_emplace(pClient->iClientId).first->second;
		if (rOwnedIds.empty())
		{
			continue;
		}

		// Find the coord for this global player ID in the client's owned list
		engine::GridCoord updateCoord {};
		bool bFound = false;
		for (int64_t i = 0; i < std::ssize(rOwnedIds); ++i)
		{
			if (rOwnedIds.at(i) == rRequest.globalId)
			{
				updateCoord = pClient->authorizedCoords.at(i);
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
			if (rPlayers.pGlobalPlayerIds[j] == rRequest.globalId)
			{
				iPlayerUuid = rPlayers.puiIds[j].ToUuid().Value();
				break;
			}
		}
		if (iPlayerUuid == 0)
		{
			continue;
		}

		uint8_t uiPendingWeaponModeTicks = static_cast<uint8_t>(engine::kiTickRate);
		StatusChange updateChange {.eType = StatusChangeType::kUpdatePlayer, .data = UpdatePlayerData{.iPlayerUuid = iPlayerUuid, .bUseMissiles = rRequest.bUseMissiles, .fNavigationDelay = rRequest.fNavigationDelay, .uiPendingWeaponModeTicks = uiPendingWeaponModeTicks}};
		frameInputIt->second.statusChanges.push_back(updateChange);

		LOG(kNetwork, kDebug, "ServerBroadcaster::ProcessUpdatePlayerRequests Client: {} GlobalPlayer: {} PlayerUuid: {} Coord: ({},{}) Missiles: {} NavDelay: {}", rRequest.iClientId, rRequest.globalId, iPlayerUuid, updateCoord.x, updateCoord.y, rRequest.bUseMissiles, rRequest.fNavigationDelay);
	}
}

void ServerBroadcaster::QueueUpdatePlayerRequest(const PendingUpdatePlayerRequest& rRequest)
{
	mPendingUpdatePlayerRequests.push_back(rRequest);
}

void ServerBroadcaster::ClearPendingRequests()
{
	mPendingUpdatePlayerRequests.clear();
}

void ServerBroadcaster::ClearSpawns()
{
	mSpawns.clear();
}

void ServerBroadcaster::ResetState()
{
	mSpawns.clear();
	mPendingUpdatePlayerRequests.clear();
}

#endif // BT_SERVER

} // namespace game
