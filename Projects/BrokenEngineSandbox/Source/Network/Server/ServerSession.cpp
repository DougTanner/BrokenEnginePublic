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
	// Heap: ComputeActiveSet/EnsureNextFrames may grow mActiveCoords and CoordFrames maps
	ScopedSuppressAllocationTracking suppress;

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
	ASSERT(common::gpMultithreading->IsMainThread());

	// Heap: SendFullState, SendAssignPlayer, and BroadcastUpdate allocate for serialization and compression
	ScopedSuppressAllocationTracking suppress;

	HandleResyncRequests();
	mpClientManager->FinalizeNewClients();
	mpClientManager->DetectPlayerDeaths();
	mpFleetManager->DetectDisconnectedPlayerDeaths();
	mpBroadcaster->BroadcastStatusChanges(iTick);
	mpBroadcaster->ClearSpawns();
	SubscriptionUpdates();
	engine::gpServer->Flush();
}

void ServerSession::SendResends(int64_t iTick)
{
	// Heap: ENet packet creation per resend in engine::Server::SendResends
	ScopedSuppressAllocationTracking suppress;
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->GetClients();
	for (engine::ClientConnection& rClient : rClients)
	{
		engine::gpServer->SendResends(rClient, iTick);
	}
}

void ServerSession::WaitForTick(engine::TimeStep& rTimeStep)
{
	std::chrono::nanoseconds scaledTickNs = rTimeStep.SimToWall(kTickNs);
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
				mpBroadcaster->QueueUpdatePlayerRequest({rPacket.iClientId, globalId, bUseMissiles, fNavigationDelay});
				break;
			}
			case GamePacketType::kClientCreateFleetRequest:
			{
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
				break;
			}
			case GamePacketType::kClientSaveRequest:
			{
				LOG(kDefault, kDebug, "ServerSession::kClientSaveRequest Client: {}", rPacket.iClientId);
				gpGame->mGameSaveLoad.ServerSave();
				break;
			}
			case GamePacketType::kClientLoadRequest:
			{
				LOG(kDefault, kDebug, "ServerSession::kClientLoadRequest Client: {}", rPacket.iClientId);
				gpGame->mGameSaveLoad.ServerLoad();
				break;
			}
			case GamePacketType::kClientResetRequest:
			{
				LOG(kDefault, kDebug, "ServerSession::kClientResetRequest Client: {}", rPacket.iClientId);
				gpGame->mGameSaveLoad.ServerReset();
				break;
			}
			case GamePacketType::kClientReplayRecordRequest:
			{
				LOG(kDefault, kDebug, "ServerSession::kClientReplayRecordRequest Client: {}", rPacket.iClientId);
				gpGame->mGameFlags.Set(engine::GameFlags::kSaveReplay);
				break;
			}
			case GamePacketType::kClientReplayPlaybackRequest:
			{
				LOG(kDefault, kDebug, "ServerSession::kClientReplayPlaybackRequest Client: {}", rPacket.iClientId);
				gpGame->mGameFlags.Set(engine::GameFlags::kLoadReplay);
				break;
			}
			case GamePacketType::kClientPauseRequest:
			{
				// 1B paused (type byte already stripped)
				if (rPacket.payload.size() < 1)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				bool bPaused = engine::ReadUint8(pCursor) != 0;
				gpGame->mGameFlags.Set(engine::GameFlags::kPaused, bPaused);
				LOG(kDefault, kDebug, "Server paused: {}", bPaused);
				break;
			}
			case GamePacketType::kClientTimespeedRequest:
			{
				// 1B direction (type byte already stripped); 0 = slower, 1 = faster
				if (rPacket.payload.size() < 1)
				{
					break;
				}
				const uint8_t* pCursor = rPacket.payload.data();
				uint8_t uiDirection = engine::ReadUint8(pCursor);
				if (uiDirection == 0)
				{
					gpGame->mTimeStep.DecreaseTimeScale();
				}
				else
				{
					gpGame->mTimeStep.IncreaseTimeScale();
				}
				BroadcastTimespeedIfChanged();
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
	ScopedSuppressAllocationTracking suppress;
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
	ScopedSuppressAllocationTracking suppress;

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

	// [1B type][8B global player ID][GridCoord]
	engine::gpServer->SendSimplePacket(pClient->pPeer, GamePacketType::kServerAssignPlayer, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, globalId.iValue, coord);
}

void ServerSession::SendPlayerState(int64_t iClientId, PlayerStateWireType eWireType, int64_t iGlobalPlayerId, engine::GridCoord coord)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	static constexpr const char* kpStateNames[] =
	{
		"Spawned",
		"ChangedFrame",
		"Died",
	};
	LOG(kNetwork, kInfo, "ServerSession::SendPlayerState State: {} Client: {} GlobalPlayer: {} Grid: ({},{})", kpStateNames[static_cast<size_t>(eWireType)], iClientId, iGlobalPlayerId, coord.x, coord.y);

	// [1B type][1B state][8B global player ID][4B coord.x][4B coord.y]
	engine::gpServer->SendSimplePacket(pClient->pPeer, GamePacketType::kServerPlayerState, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(eWireType), iGlobalPlayerId, coord);
}

void ServerSession::BroadcastTimespeedIfChanged()
{
	if (!gpGame->mTimeStep.mbTimeScaleChanged) [[likely]]
	{
		return;
	}
	gpGame->mTimeStep.mbTimeScaleChanged = false;

	int64_t iMultiply = gpGame->mTimeStep.miTimeMultiply;
	int64_t iDivide = gpGame->mTimeStep.miTimeDivide;
	LOG(kNetwork, kDebug, "ServerSession::BroadcastTimespeedIfChanged Multiply: {} Divide: {}", iMultiply, iDivide);

	for (engine::ClientConnection& rClient : engine::gpServer->GetClients())
	{
		if (!rClient.bHandshakeComplete)
		{
			continue;
		}
		// [1B type][8B multiply][8B divide]
		engine::gpServer->SendSimplePacket(rClient.pPeer, GamePacketType::kServerTimespeedUpdate, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, iMultiply, iDivide);
	}
}

void ServerSession::SendTimespeedToNewClient(ENetPeer* pPeer)
{
	if (gpGame->mTimeStep.miTimeMultiply == 1 && gpGame->mTimeStep.miTimeDivide == 1)
	{
		return;
	}
	// [1B type][8B multiply][8B divide]
	engine::gpServer->SendSimplePacket(pPeer, GamePacketType::kServerTimespeedUpdate, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, gpGame->mTimeStep.miTimeMultiply, gpGame->mTimeStep.miTimeDivide);
}

void ServerSession::SubscriptionUpdates()
{
	SendNewSubscriptionFullStates();

	std::vector<SubscriptionUpdate>& rPendingUpdates = mpTransferManager->mPendingSubscriptionUpdates;
	if (rPendingUpdates.empty())
	{
		return;
	}

	// Client handles subscriptions — server just sends player assignment with global ID
	for (const SubscriptionUpdate& rUpdate : rPendingUpdates)
	{
		SendAssignPlayer(rUpdate.iClientId, rUpdate.globalPlayerId, rUpdate.newCoord);
		SendPlayerState(rUpdate.iClientId, PlayerStateWireType::kChangedFrame, rUpdate.globalPlayerId.iValue, rUpdate.newCoord);
	}

	rPendingUpdates.clear();
}

void ServerSession::HandleResyncRequests()
{
	std::vector<int64_t>& rResyncClientIds = engine::gpServer->DrainPendingResyncClientIds();
	if (rResyncClientIds.empty())
	{
		return;
	}

	// Heap: per-resync per-slot SendCoordFullState allocates serialization buffers
	ScopedSuppressAllocationTracking suppress;

	for (int64_t iClientId : rResyncClientIds)
	{
		engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
		if (pClient == nullptr)
		{
			continue;
		}

		LOG(kNetwork, kError, "ServerSession::HandleResyncRequests Client: {}", iClientId);

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
	// Heap: re-link rebuilds owned-id vectors and authorizedCoords; pending state cleared across managers
	ScopedSuppressAllocationTracking suppress;

	engine::gpServer->BroadcastLoadNotification();

	mpFleetManager->mNavigation.ClearPendingFlagshipUpdates();
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

		if (!TryRelinkClientForLoad(rClient, rLoadOwnedIds))
		{
			LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad Client: {} no GUID match, will respawn", rClient.iClientId);
		}

		mpFleetManager->OnResetForLoad(rClient.iClientId, rClient.clientGuid);
	}

	// Clear all pending state across managers
	mpClientManager->ResetState();
	mpTransferManager->ResetState();
	mpBroadcaster->ResetState();
	// Fleet manager: only drop pending request queues. mFleets / mPlayerToGuid / mGuidToClientId
	// were just authoritatively restored by ReadFleetData + per-client OnResetForLoad above;
	// a full ResetState() here would annihilate that restoration.
	mpFleetManager->ClearPendingRequests();
	// Pending flagship updates were cleared at the start of this function via mNavigation.ClearPendingFlagshipUpdates(); fleet restoration above re-queued entries — do NOT clear again here.

	// Clear stale ring buffers and pending events
	engine::gpServer->ClearBufferedFrames();
	engine::gpServer->DrainPendingSpawnRequests().clear();
	engine::gpServer->DrainPendingNewSubscriptions().clear();
	engine::gpServer->DrainPendingResyncClientIds().clear();

	engine::gpServer->Flush();
}

bool ServerSession::TryRelinkClientForLoad(engine::ClientConnection& rClient, std::vector<engine::global_id_t>& rLoadOwnedIds)
{
	if (rClient.clientGuid.IsEmpty())
	{
		return false;
	}

	bool bRelinked = false;
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
				SendPlayerState(rClient.iClientId, PlayerStateWireType::kSpawned, globalId.iValue, rCoord);
				LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad Re-linked Client: {} GlobalPlayer: {} Coord: ({},{})", rClient.iClientId, globalId, rCoord.x, rCoord.y);
				bRelinked = true;
			}
		}
	}
	return bRelinked;
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
