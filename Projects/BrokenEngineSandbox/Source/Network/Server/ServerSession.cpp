#include "Pch.h"

#include "Network/Server/ServerSession.h"

#include "Network/NetworkCursor.h"

#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/GamePacketType.h"
#include "Network/PlayerEvents.h"
#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerClientManager.h"
#include "Network/Server/ServerFleetManager.h"
#include "Network/Server/ServerTransferManager.h"

namespace game
{

#if defined(BT_SERVER)

ServerSession::ServerSession()
{
	gpServerSession = this;
	mpFleetManager = std::make_unique<ServerFleetManager>();
	mpTransferManager = std::make_unique<ServerTransferManager>();
	mpBroadcaster = std::make_unique<ServerBroadcaster>();
	mpClientManager = std::make_unique<ServerClientManager>();
}

ServerSession::~ServerSession()
{
	mpClientManager.reset();
	mpBroadcaster.reset();
	mpTransferManager.reset();
	mpFleetManager.reset();
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
	mpClientManager->FinalizeNewClients(iTick);
	mpClientManager->DetectPlayerDeaths();
	mpFleetManager->DetectDisconnectedPlayerDeaths();
	mpBroadcaster->BroadcastStatusChanges(iTick);
	mpBroadcaster->ClearSpawns();
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

void ServerSession::ParseReceivedGamePackets()
{
	for (const engine::ReceivedGamePacket& rPacket : engine::gpServer->DrainReceivedGamePackets())
	{
		GamePacketType eType = static_cast<GamePacketType>(rPacket.uiPacketType);

		switch (eType)
		{
			case GamePacketType::kClientUpdatePlayerRequest:
			{
				// 8B global player ID + 1B bUseMissiles + 4B fNavigationDelay = 13 bytes (type byte already stripped)
				if (rPacket.payload.size() < 13)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				engine::global_id_t globalId {};
				globalId.iValue = engine::ReadInt64(pCursor);
				bool bUseMissiles = engine::ReadUint8(pCursor) != 0;
				float fNavigationDelay = engine::ReadFloat(pCursor);
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::UpdatePlayer Client: {} GlobalPlayer: {} Missiles: {} NavDelay: {}", rPacket.iClientId, globalId, bUseMissiles, fNavigationDelay);
				mpBroadcaster->QueueUpdatePlayerRequest({rPacket.iClientId, globalId, bUseMissiles, fNavigationDelay});
				break;
			}
			case GamePacketType::kClientCreateFleetRequest:
			{
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::CreateFleet Client: {}", rPacket.iClientId);
				mpFleetManager->QueueCreateRequest({rPacket.iClientId});
				break;
			}
			case GamePacketType::kClientDeleteFleetRequest:
			{
				if (rPacket.payload.size() < 8)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				int64_t iFleetIndex = engine::ReadInt64(pCursor);
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::DeleteFleet Client: {} Fleet: {}", rPacket.iClientId, iFleetIndex);
				mpFleetManager->QueueDeleteRequest({rPacket.iClientId, iFleetIndex});
				break;
			}
			case GamePacketType::kClientSpawnIntoFleetRequest:
			{
				// 8B fleetIndex = 8 bytes (type byte already stripped)
				if (rPacket.payload.size() < 8)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				int64_t iFleetIndex = engine::ReadInt64(pCursor);
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::SpawnIntoFleet Client: {} Fleet: {}", rPacket.iClientId, iFleetIndex);
				mpFleetManager->QueueSpawnIntoRequest({rPacket.iClientId, iFleetIndex});
				break;
			}
			case GamePacketType::kClientRespawnInFleetRequest:
			{
				// 8B fleetIndex + 8B memberIndex = 16 bytes (type byte already stripped)
				if (rPacket.payload.size() < 16)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				int64_t iFleetIndex = engine::ReadInt64(pCursor);
				int64_t iMemberIndex = engine::ReadInt64(pCursor);
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::RespawnInFleet Client: {} Fleet: {} Member: {}", rPacket.iClientId, iFleetIndex, iMemberIndex);
				mpFleetManager->QueueRespawnRequest({rPacket.iClientId, iFleetIndex, iMemberIndex});
				break;
			}
			case GamePacketType::kClientFleetNavigationDelay:
			{
				// 8B fleetIndex + 4B delay = 12 bytes (type byte already stripped)
				if (rPacket.payload.size() < 12)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				int64_t iFleetIndex = engine::ReadInt64(pCursor);
				float fDelay = engine::ReadFloat(pCursor);
				const engine::ClientConnection* pClient = engine::gpServer->FindClient(rPacket.iClientId);
				if (pClient != nullptr)
				{
					mpFleetManager->UpdateFleetNavigationDelay(pClient->clientGuid, iFleetIndex, fDelay);
				}
				LOG(kNetwork, kDebug, "ParseReceivedGamePackets::FleetNavigationDelay Client: {} Fleet: {} Delay: {}", rPacket.iClientId, iFleetIndex, fDelay);
				break;
			}
			default:
				break;
		}
	}
}

void ServerSession::PreTickNetwork()
{
	// Heap: ENet polling and game server methods allocate vectors for inputs, spawns, and status changes
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	mpBroadcaster->ClearPendingRequests();
	mpFleetManager->ClearPendingRequests();
	PollNetworkBase();
	ParseReceivedGamePackets();
	mpClientManager->Disconnects();
	mpClientManager->NewClients();
	mpFleetManager->ProcessCreateFleetRequests();
	mpFleetManager->ProcessDeleteFleetRequests();
	mpFleetManager->ProcessSpawnIntoFleetRequests();
	mpFleetManager->ProcessRespawnInFleetRequests();
	mpClientManager->ProcessSpawnRequests();
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
	for (const PendingPlayerDestroy& rDestroy : mpClientManager->mPendingPlayerDestroys)
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

void ServerSession::SendAssignPlayer(int64_t iClientId, engine::global_id_t globalId, engine::GridCoord coord)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kInfo, "ServerSession::SendAssignPlayer Client: {} GlobalPlayer: {} Grid: ({},{})", iClientId, globalId, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B global player ID][GridCoord]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerAssignPlayer));
	rWorkbuffer.PushBack<int64_t>(globalId.iValue);
	engine::WriteGridCoord(rWorkbuffer, coord);

	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void ServerSession::SendPlayerState(int64_t iClientId, uint8_t uiStateType, int64_t iGlobalPlayerId, engine::GridCoord coord)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kInfo, "ServerSession::SendPlayerState State: {} Client: {} GlobalPlayer: {} Grid: ({},{})", static_cast<int>(uiStateType), iClientId, iGlobalPlayerId, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B state][8B global player ID][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerPlayerState));
	rWorkbuffer.PushBack<uint8_t>(uiStateType);
	rWorkbuffer.PushBack<int64_t>(iGlobalPlayerId);
	engine::WriteGridCoord(rWorkbuffer, coord);

	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void ServerSession::SubscriptionUpdates([[maybe_unused]] int64_t iTick)
{
	SendNewSubscriptionFullStates(iTick);

	std::vector<SubscriptionUpdate>& rPendingUpdates = mpTransferManager->mPendingSubscriptionUpdates;
	if (rPendingUpdates.empty())
	{
		return;
	}

	// Client handles subscriptions — server just sends player assignment with global ID
	for (const SubscriptionUpdate& rUpdate : rPendingUpdates)
	{
		SendAssignPlayer(rUpdate.iClientId, rUpdate.globalPlayerId, rUpdate.newCoord);
		SendPlayerState(rUpdate.iClientId, PlayerEventTypeToWire(PlayerEventType::kChangedFrame), rUpdate.globalPlayerId.iValue, rUpdate.newCoord);
	}

	rPendingUpdates.clear();
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

		LOG(kNetwork, kWarning, "ServerSession::HandleResyncRequests Client: {}", iClientId);

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
	LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad");
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	engine::gpServer->BroadcastLoadNotification();

	mpFleetManager->mPendingFlagshipUpdates.clear();
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
		std::vector<engine::global_id_t>& rLoadOwnedIds = mClientOwnedPlayerIds.try_emplace(rClient.iClientId).first->second;
		rLoadOwnedIds.clear();
		rClient.authorizedCoords.clear();

		if (!rClient.clientGuid.IsEmpty())
		{
			for (const auto& [rCoord, rFrames] : gpGame->mCoordFrames)
			{
				const PlayersPostRender& rPlayers = *rFrames.pCurrent->postRender.pPlayers;
				for (int64_t i = 0; i < rPlayers.iCount; ++i)
				{
					if (rPlayers.pClientGuids[i] == rClient.clientGuid)
					{
						engine::global_id_t globalId = rPlayers.pGlobalPlayerIds[i];
						rLoadOwnedIds.push_back(globalId);
						rClient.authorizedCoords.push_back(rCoord);

						SendAssignPlayer(rClient.iClientId, globalId, rCoord);
						SendPlayerState(rClient.iClientId, PlayerEventTypeToWire(PlayerEventType::kSpawned), globalId.iValue, rCoord);
						LOG(kDefault, kDebug, "ResetClientsForLoad Client: {} re-linked to GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId, rCoord.x, rCoord.y);
					}
				}
			}
		}

		if (rLoadOwnedIds.empty())
		{
			LOG(kDefault, kDebug, "ResetClientsForLoad Client: {} no GUID match, will respawn", rClient.iClientId);
		}

		mpFleetManager->OnResetForLoad(rClient.iClientId, rClient.clientGuid);
	}

	// Clear all pending state across managers
	mpClientManager->ResetState();
	mpTransferManager->ResetState();
	mpBroadcaster->ResetState();
	mpFleetManager->ResetState();
	// mPendingFlagshipUpdates intentionally NOT cleared — fleet restoration above may queue updates

	// Clear stale ring buffers and pending events
	engine::gpServer->ClearBufferedFrames();
	engine::gpServer->DrainPendingSpawnRequests().clear();
	engine::gpServer->DrainPendingNewSubscriptions().clear();
	engine::gpServer->DrainPendingResyncClientIds().clear();

	engine::gpServer->Flush();
}

void ServerSession::WriteFleetData(std::fstream& rFileStream) const
{
	mpFleetManager->WriteFleetData(rFileStream);
}

void ServerSession::ReadFleetData(std::fstream& rFileStream)
{
	mpFleetManager->ReadFleetData(rFileStream);
}

#endif // BT_SERVER

} // namespace game
