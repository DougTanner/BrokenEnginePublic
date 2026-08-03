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
	ScopedSuppressAllocationTracking suppress;

	gpGame->mFrameInputs.clear();
	mBroadcastStatusChanges.clear();

	// Initialize FrameInputs for all active coordinates
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		gpGame->mFrameInputs.try_emplace(rCoord);
	}

	const bool bAdvancing = gpGame->mfLastDeltaTime > 0.0f;
	if (bAdvancing)
	{
		std::erase_if(gpServerSession->mpClientManager->mClientsWaitingForSpawn, [&](const ClientSpawnInfo& rClientSpawnInformation)
		{
			if (!rClientSpawnInformation.fleetGuid.IsValid())
			{
				return false;
			}

			ServerFleetManager::FleetLookupResult result = gpServerSession->mpFleetManager->LookupFleetWantedCoord(rClientSpawnInformation.clientGuid, rClientSpawnInformation.fleetGuid, rClientSpawnInformation.iMemberIndex);
			if (result.flags & ServerFleetManager::FleetLookupFlags::kFound)
			{
				return false;
			}

			LOG(kNetwork, kWarning, "ServerBroadcaster::BuildFrameInputs Dropping queued spawn Client: {} FleetGuid: ({},{})", rClientSpawnInformation.iClientId, rClientSpawnInformation.fleetGuid.uiHigh, rClientSpawnInformation.fleetGuid.uiLow);
			return true;
		});

		// Add spawn StatusChanges for clients waiting for initial spawn
		for (const ClientSpawnInfo& rClientSpawnInformation : gpServerSession->mpClientManager->mClientsWaitingForSpawn)
		{
			int64_t iGlobalId = gpGame->GenerateGlobalId();

			bool bIsFlagship = false;
			engine::GridCoord spawnFleetWantedCoord {};
			uint8_t uiSpawnPendingFleetTicks = 0;
			if (rClientSpawnInformation.fleetGuid.IsValid())
			{
				ServerFleetManager::FleetLookupResult result = gpServerSession->mpFleetManager->LookupFleetWantedCoord(rClientSpawnInformation.clientGuid, rClientSpawnInformation.fleetGuid, rClientSpawnInformation.iMemberIndex);
				bIsFlagship = result.flags & ServerFleetManager::FleetLookupFlags::kIsFlagship;
				spawnFleetWantedCoord = result.fleetWantedCoord;
				uiSpawnPendingFleetTicks = result.uiPendingFleetWantedCoordTicks;
			}

			StatusChange spawnChange {.eType = StatusChangeType::kSpawnPlayer, .data = SpawnPlayerData{.iGlobalId = iGlobalId, .bIsFlagship = bIsFlagship, .fleetWantedCoord = spawnFleetWantedCoord, .uiPendingFleetWantedCoordTicks = uiSpawnPendingFleetTicks}};
			gpGame->mFrameInputs.try_emplace(engine::kOriginCoord).first->second.statusChanges.push_back(spawnChange);
			LOG(kNetwork, kVerbose, "ServerBroadcaster::BuildFrameInputs::kSpawnPlayer Client: {} GlobalId: {} Coord: ({},{}) Flagship: {}", rClientSpawnInformation.iClientId, iGlobalId, engine::kOriginCoord.x, engine::kOriginCoord.y, bIsFlagship);
		}

		// Inject weapon mode toggle StatusChanges
		ProcessUpdatePlayerRequests();

		// Tick fleet timers and inject fleet coord updates only when this update advances. Subtracting
		// a zero delta does not make TickFleetTimers inert: an already-expired timer can still fire,
		// consume random state, and queue an update that no frame tick could consume. Deferring the
		// timer and all queued work here applies them in order on the first advancing update.
		gpServerSession->mpFleetManager->TickFleetTimers();
		gpServerSession->mpFleetManager->ProcessFlagshipUpdates();

		// Drain agent-injected StatusChanges into mFrameInputs so they ride the same broadcast / CRC / replay channel
		// as real spawns. Entries not consumable this update stay in the map (deferred) and apply on the first update
		// that can take them, matching the paused deferral. Whole-map defers: the update won't tick (mfLastDeltaTime == 0:
		// paused / zero-accumulated ticks — the per-tick consumer loop won't run and the next BuildFrameInputs wipes
		// mFrameInputs); replay playback (LoadDifference overwrites mFrameInputs from the recorded stream); or a client
		// sits in mClientsWaitingForSpawn (an agent spawn landing the same tick would corrupt the spawn-assignment-by-
		// snapshot-diff zip). Per-coord defers below: a coord the tick loop won't simulate (inactive, or no committed
		// pCurrent frame yet).
		if (gpGame->mfLastDeltaTime > 0.0f && !gpGame->mGameSaveLoad.IsReplaying() && gpServerSession->mpClientManager->mClientsWaitingForSpawn.empty())
		{
			for (auto it = mPendingAgentStatusChanges.begin(); it != mPendingAgentStatusChanges.end();)
			{
				const engine::GridCoord& rCoord = it->first;
				auto framesIt = gpGame->mCoordFrames.find(rCoord);
				bool bActive = std::find(gpGame->mActiveCoords.begin(), gpGame->mActiveCoords.end(), rCoord) != gpGame->mActiveCoords.end();
				if (!bActive || framesIt == gpGame->mCoordFrames.end() || framesIt->second.pCurrent == nullptr)
				{
					++it;
					continue;
				}
				std::vector<StatusChange>& rStatusChanges = gpGame->mFrameInputs.try_emplace(rCoord).first->second.statusChanges;
				rStatusChanges.insert(rStatusChanges.end(), it->second.begin(), it->second.end());
				// Sort by type: the broadcast batch emits type-grouped ascending (SerializeStatusChangeBatch) and clients
				// apply in that wire order, so the server must tick the same order or swap-pop indices diverge → CRC desync.
				// Stable sort preserves within-type insertion order, matching the serializer's stable type-group scan.
				std::stable_sort(rStatusChanges.begin(), rStatusChanges.end(), [](const StatusChange& rLeft, const StatusChange& rRight)
				{
					return rLeft.eType < rRight.eType;
				});
				it = mPendingAgentStatusChanges.erase(it);
			}
		}

		// Save StatusChanges for broadcasting (transfers handled separately in HarvestTransfers)
		for (const auto& [rCoord, rFrameInput] : gpGame->mFrameInputs)
		{
			if (!rFrameInput.statusChanges.empty())
			{
				mBroadcastStatusChanges.insert_or_assign(rCoord, rFrameInput.statusChanges);
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
}

void ServerBroadcaster::BuildTickPublication(int64_t iTick, engine::ServerSessionRuntime& rRuntime, common::ScopedWorkbufferArena& rPublicationArena)
{
	(void)rPublicationArena;
	const std::unordered_map<engine::GridCoord, std::vector<StatusChange>>& rTransfers = gpServerSession->mpTransferManager->mTransfers;

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	int64_t iActiveCoordCount = std::ssize(gpGame->mActiveCoords);

	auto gridUpdatesAlloc = rWorkbuffer.PushBuffer<std::pair<engine::GridCoord, engine::GridUpdateData>*>(iActiveCoordCount * static_cast<int64_t>(sizeof(std::pair<engine::GridCoord, engine::GridUpdateData>)));
	std::pair<engine::GridCoord, engine::GridUpdateData>* pGridUpdates = gridUpdatesAlloc;
	int64_t iGridUpdateCount = 0;
	common::ScopedWorkbufferArena statusChanges = rWorkbuffer.Push();
	for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
	{
		engine::GridUpdateData updateData {};
		updateData.sharedCrc = gpGame->CurrentFrame(rCoord).postRender.sharedCrc;
		int64_t iRunStart = statusChanges.Count<StatusChange>();
		if (auto it = mBroadcastStatusChanges.find(rCoord); it != mBroadcastStatusChanges.end())
		{
			for (const StatusChange& rChange : it->second)
			{
				statusChanges.PushBack(rChange);
			}
		}
		if (auto it = rTransfers.find(rCoord); it != rTransfers.end())
		{
			for (const StatusChange& rChange : it->second)
			{
				statusChanges.PushBack(rChange);
			}
		}
		int64_t iRunCount = statusChanges.Count<StatusChange>() - iRunStart;
		if (iRunCount > 0)
		{
			updateData.statusChanges = {statusChanges.Data<StatusChange>() + iRunStart, static_cast<size_t>(iRunCount)};
			LOG(kNetwork, kVerbose, "ServerBroadcaster::BuildTickPublication Coord: ({},{}) Tick: {} StatusChanges: {}", rCoord.x, rCoord.y, iTick, iRunCount);
		}
		pGridUpdates[iGridUpdateCount++] = {rCoord, updateData};
	}

	if constexpr (kbDesyncDebugFrames)
	{
		auto fullFramesAlloc = rWorkbuffer.PushBuffer<std::pair<engine::GridCoord, const game::Frame*>*>(iActiveCoordCount * static_cast<int64_t>(sizeof(std::pair<engine::GridCoord, const game::Frame*>)));
		std::pair<engine::GridCoord, const game::Frame*>* pFullFrames = fullFramesAlloc;
		int64_t iFullFrameCount = 0;
		for (const engine::GridCoord& rCoord : gpGame->mActiveCoords)
		{
			pFullFrames[iFullFrameCount++] = {rCoord, &gpGame->CurrentFrame(rCoord)};
		}
		rRuntime.PublishTick(iTick, pGridUpdates, iGridUpdateCount, pFullFrames, iFullFrameCount);
	}
	else
	{
		rRuntime.PublishTick(iTick, pGridUpdates, iGridUpdateCount, nullptr, 0);
	}
}

void ServerBroadcaster::ProcessUpdatePlayerRequests()
{
	// Heap: pending player-update requests may grow frame status changes
	ScopedSuppressAllocationTracking suppress;

	for (const PendingUpdatePlayerRequest& rRequest : mPendingUpdatePlayerRequests)
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(rRequest.iClientId);
		if (pClient == nullptr)
		{
			continue;
		}
		std::span<const OwnedPlayer> ownedPlayers = gpServerSession->mClientPlayers.Owned(pClient->iClientId);
		if (ownedPlayers.empty())
		{
			continue;
		}

		// Find the coord for this global player ID in the client's owned list
		engine::GridCoord updateCoord {};
		bool bFound = false;
		for (const OwnedPlayer& rOwnedPlayer : ownedPlayers)
		{
			if (rOwnedPlayer.globalId == rRequest.globalId)
			{
				updateCoord = rOwnedPlayer.coord;
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

		LOG(kNetwork, kDebug, "ServerBroadcaster::ProcessUpdatePlayerRequests Client: {} GlobalPlayer: {} PlayerUuid: {} Coord: ({},{}) Missiles: {} NavDelay: {}", rRequest.iClientId, rRequest.globalId, iPlayerUuid, updateCoord.x, updateCoord.y, rRequest.bUseMissiles, common::Wb(rRequest.fNavigationDelay, 3));
	}
}

void ServerBroadcaster::QueueUpdatePlayerRequest(const PendingUpdatePlayerRequest& rRequest)
{
	mPendingUpdatePlayerRequests.push_back(rRequest);
}

void ServerBroadcaster::QueueAgentStatusChange(engine::GridCoord coord, const StatusChange& rChange)
{
	// Runs at the agent command drain point (top of ServerUpdate) under AgentCommandServer::Drain's blanket
	// allocation suppression — mirrors QueueUpdatePlayerRequest (no independent inner guard).
	mPendingAgentStatusChanges.try_emplace(coord).first->second.push_back(rChange);
}

void ServerBroadcaster::ClearPendingRequests()
{
	mPendingUpdatePlayerRequests.clear();
}

void ServerBroadcaster::ClearBroadcastStatusChanges()
{
	mBroadcastStatusChanges.clear();
}

void ServerBroadcaster::ResetState()
{
	mBroadcastStatusChanges.clear();
	mPendingUpdatePlayerRequests.clear();
	// Drop any paused-deferred agent injection so a globalId minted against the pre-load game can't leak a stale
	// StatusChange into freshly-loaded frames. Not cleared in ClearPendingRequests (runs before the agent Drain).
	mPendingAgentStatusChanges.clear();
}

#endif // BT_SERVER

} // namespace game
