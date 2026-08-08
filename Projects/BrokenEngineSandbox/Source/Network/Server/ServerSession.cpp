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
#include "Network/Server/ServerFleetSerialization.h"
#include "Network/Server/ServerTransferManager.h"

namespace game
{

#if defined(BT_SERVER)

ServerSession::ServerSession()
{
	ASSERT(gpServerSession == nullptr);
	gpServerSession = this;
	mpFleetManager = std::make_unique<ServerFleetManager>();
	mpTransferManager = std::make_unique<ServerTransferManager>();
	mpBroadcaster = std::make_unique<ServerBroadcaster>();
	mpClientManager = std::make_unique<ServerClientManager>();
	mpRuntime = std::make_unique<engine::ServerSessionRuntime>(*this, engine::kuiDefaultPort);
}

ServerSession::~ServerSession()
{
	mpRuntime.reset();
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

// Clamp a wire-supplied navigation delay before it enters server-authoritative sim state.
// Range [0.0f, 60.0f] matches the UI slider (HudScreen.cpp); NaN/Inf substitute the Fleet::fNavigationDelay default (60.0f)
// so a hostile non-finite value can't freeze fleet navigation (every fFrameChangeTimer <= 0 comparison against NaN is false).
static float ValidateNavigationDelay(float fDelay)
{
	return std::isfinite(fDelay) ? std::clamp(fDelay, 0.0f, 60.0f) : 60.0f;
}

void ServerSession::ParseReceivedGamePackets()
{
	for (const engine::ReceivedGamePacket& rPacket : mpRuntime->mpServer->mReceivedGamePackets)
	{
		GamePacketType eType = static_cast<GamePacketType>(rPacket.uiPacketType);

		// Contract gate (trust boundary): validate every game-range packet once before dispatch (drop -> count -> escalate).
		// The per-case size checks and ValidateNavigationDelay clamps below remain as backstops.
		engine::ClientConnection* pGateClient = engine::gpServer->FindClient(rPacket.iClientId);
		if (pGateClient == nullptr)
		{
			// Client removed mid-drain (an earlier violation disconnect purged it) — skip silently.
			continue;
		}

		engine::ClientPacketContract contract = NetworkSessionContract::GetClientPacketContract(eType);
		int64_t iFullSize = static_cast<int64_t>(rPacket.payload.size()) + 1; // + type byte (already stripped from payload)

		if (contract.iMaxSize == 0)
		{
			// Sentinel: not client-sendable (server->client, unknown, or debug-control on a non-debug server).
			// RecordContractViolation may remove the client — do not touch pClient afterward.
			engine::gpServer->RecordContractViolation(rPacket.iClientId, "game type not client-sendable", rPacket.uiPacketType, iFullSize);
			continue;
		}
		if (iFullSize < contract.iMinSize || iFullSize > contract.iMaxSize)
		{
			engine::gpServer->RecordContractViolation(rPacket.iClientId, "game packet size out of range", rPacket.uiPacketType, iFullSize);
			continue;
		}
		// Per-type per-tick cap. tickTypeCounts is reset once per update by the engine (both of the update's polls
		// share the window); engine and game types occupy disjoint type-byte ranges, so sharing one array across
		// both dispatch points is coherent within that window.
		if (++pGateClient->tickTypeCounts[rPacket.uiPacketType] > contract.iMaxPerTick)
		{
			if (contract.bOverCapCountsViolation)
			{
				engine::gpServer->RecordContractViolation(rPacket.iClientId, "game packet per-tick cap exceeded", rPacket.uiPacketType, iFullSize);
			}
			continue; // drop
		}

		try
		{
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
					float fNavigationDelay = ValidateNavigationDelay(engine::ReadFloat(pCursor));
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
					// 16B fleetGuid = 16 bytes (type byte already stripped)
					if (rPacket.payload.size() < 16)
					{
						break;
					}
					const uint8_t* pCursor = rPacket.payload.data();
					FleetGuid fleetGuid {};
					fleetGuid.uiHigh = engine::ReadUint64(pCursor);
					fleetGuid.uiLow = engine::ReadUint64(pCursor);
					mpFleetManager->QueueDeleteRequest({rPacket.iClientId, fleetGuid});
					break;
				}
				case GamePacketType::kClientSpawnIntoFleetRequest:
				{
					// 16B fleetGuid = 16 bytes (type byte already stripped)
					if (rPacket.payload.size() < 16)
					{
						break;
					}
					const uint8_t* pCursor = rPacket.payload.data();
					FleetGuid fleetGuid {};
					fleetGuid.uiHigh = engine::ReadUint64(pCursor);
					fleetGuid.uiLow = engine::ReadUint64(pCursor);
					mpFleetManager->QueueSpawnIntoRequest({rPacket.iClientId, fleetGuid});
					break;
				}
				case GamePacketType::kClientRespawnInFleetRequest:
				{
					// 16B fleetGuid + 8B memberIndex = 24 bytes (type byte already stripped)
					if (rPacket.payload.size() < 24)
					{
						break;
					}
					const uint8_t* pCursor = rPacket.payload.data();
					FleetGuid fleetGuid {};
					fleetGuid.uiHigh = engine::ReadUint64(pCursor);
					fleetGuid.uiLow = engine::ReadUint64(pCursor);
					int64_t iMemberIndex = engine::ReadInt64(pCursor);
					mpFleetManager->QueueRespawnRequest({rPacket.iClientId, fleetGuid, iMemberIndex});
					break;
				}
				case GamePacketType::kClientFleetNavigationDelay:
				{
					// 16B fleetGuid + 4B delay = 20 bytes (type byte already stripped)
					if (rPacket.payload.size() < 20)
					{
						break;
					}
					const uint8_t* pCursor = rPacket.payload.data();
					FleetGuid fleetGuid {};
					fleetGuid.uiHigh = engine::ReadUint64(pCursor);
					fleetGuid.uiLow = engine::ReadUint64(pCursor);
					float fDelay = ValidateNavigationDelay(engine::ReadFloat(pCursor));
					const engine::ClientConnection* pClient = engine::gpServer->FindClient(rPacket.iClientId);
					if (pClient != nullptr)
					{
						mpFleetManager->UpdateFleetNavigationDelay(pClient->clientGuid, fleetGuid, fDelay);
					}
					break;
				}
				case GamePacketType::kClientSaveRequest:
				{
					LOG(kDefault, kDebug, "ServerSession::kClientSaveRequest Client: {}", rPacket.iClientId);
					if (!gpGame->mGameSaveLoad.ServerSave())
					{
						LOG(kDefault, kWarning, "ServerSession::kClientSaveRequest ServerSave failed");
					}
					break;
				}
				case GamePacketType::kClientLoadRequest:
				{
					LOG(kDefault, kDebug, "ServerSession::kClientLoadRequest Client: {}", rPacket.iClientId);
					if (!gpGame->mGameSaveLoad.ServerLoad())
					{
						// Corrupt/truncated save: ReadGrid already left a clean-slate grid, but ServerLoad's success
						// tail (client reset + active-set recompute) never ran. Fall back exactly like ServerReset
						// (fresh frame + reset connected clients for load) rather than ticking a torn grid.
						LOG(kDefault, kError, "ServerSession::kClientLoadRequest ServerLoad failed; resetting to fresh game");
						gpGame->mGameSaveLoad.ServerReset();
					}
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
					StepTimescale(uiDirection != 0);
					break;
				}
				default:
					break;
			}
		}
		catch (const std::exception& rException)
		{
			// Trust boundary: an untrusted game packet's handler can throw (corrupt count/size from a reader,
			// .at(), file I/O). ParseReceivedGamePackets runs after the engine network poll — a different call stack than
			// engine Server::Receive — so an uncaught throw would tear down ServerUpdate. Drop the single
			// packet and continue, parity with Server::Receive/Client::Receive.
			LOG(kNetwork, kDebug, "ServerSession::ParseReceivedGamePackets dropped corrupt packet (type {}) Client: {}: {}", static_cast<uint8_t>(eType), rPacket.iClientId, rException.what());
			// Count the throw as a contract violation (drop -> count -> escalate). Do not touch any client pointer afterward.
			engine::gpServer->RecordContractViolation(rPacket.iClientId, "game packet handler threw", rPacket.uiPacketType, static_cast<int64_t>(rPacket.payload.size()) + 1);
		}
	}
}

void ServerSession::BeforeNetworkPoll()
{
	// mfLastDeltaTime still describes the previous update here. A zero-delta update could not consume
	// injected player requests, so retain them; a positive delta keeps their existing one-update lifetime.
	if (gpGame->mfLastDeltaTime > 0.0f)
	{
		mpBroadcaster->ClearPendingRequests();
	}
	// Fleet request queues drain inside their own Process*Requests, so there is nothing left to clear here.
}

void ServerSession::AfterNetworkPoll()
{
	ParseReceivedGamePackets();
	mpClientManager->Disconnects();
	mpClientManager->NewClients();
	// Ordering contract: Create runs first so requests naming a fleet created in the same poll can
	// resolve its guid. SpawnInto before Respawn also matters: both append to the client manager's
	// spawn queue, and spawn assignment pairs new player IDs with waiting clients in request order.
	mpFleetManager->ProcessCreateFleetRequests();
	mpFleetManager->ProcessDeleteFleetRequests();
	mpFleetManager->ProcessSpawnIntoFleetRequests();
	mpFleetManager->ProcessRespawnInFleetRequests();
}

void ServerSession::FinalizeTickClients()
{
	if (!gpGame->mGameSaveLoad.IsReplaying())
	{
		mpClientManager->FinalizeNewClients();
	}
	mpClientManager->DetectPlayerDeaths();
	mpFleetManager->DetectDisconnectedPlayerDeaths();
}

void ServerSession::PreparePausedSubscriptions()
{
	for (const engine::PendingNewSubscription& rSub : mpRuntime->mpServer->mPendingNewSubscriptions)
	{
		auto it = gpGame->mCoordFrames.find(rSub.coord);
		if (it == gpGame->mCoordFrames.end())
		{
			continue;
		}
		engine::FrameStaticData& rStaticData = it->second.staticData;
		if (!rStaticData.bNavDataBuilt && !rStaticData.islands.empty())
		{
			// Heap: BuildCellNavData grows the navData vertex, polygon, and visibility-edge vectors, and
			// this path runs on the main thread inside the armed main loop
			ScopedSuppressAllocationTracking suppress;
			engine::BuildCellNavData(rStaticData.navData, rStaticData.islands);
			rStaticData.bNavDataBuilt = true;
		}
	}
}

void ServerSession::AddSubscribedCoords()
{
	const std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;
	for (const engine::ClientConnection& rClient : rClients)
	{
		for (int64_t i = 0; i < std::ssize(rClient.slots); ++i)
		{
			if (rClient.slots.at(i).subscription.flags & engine::SubscriptionFlags::kActive)
			{
				engine::GridCoord coord = rClient.slots.at(i).subscription.coord;
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

	// Delete frames outside the active set, handing a recording writer its final complete current frame first.
	for (auto it = gpGame->mCoordFrames.begin(); it != gpGame->mCoordFrames.end();)
	{
		if (std::ranges::contains(gpGame->mActiveCoords, it->first))
		{
			++it;
			continue;
		}

		gpGame->mGameSaveLoad.RetainReplayEndFrame(it->first, it->second.pCurrent);
		it = gpGame->mCoordFrames.erase(it);
	}
}

void ServerSession::ComputeActiveSet()
{
	// Heap: mActiveCoords vector clear/push_back may allocate. Persists as Game member across frame updates
	ScopedSuppressAllocationTracking suppress;

	gpGame->mActiveCoords.clear();
	AddSubscribedCoords();
	EnsurePlayerCoords();

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

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerAssignPlayer));
	GameMessages::AssignPlayerMessage message {.iGlobalPlayerId = globalId.iValue, .coord = coord};
	engine::NetworkMessages::Write(rWorkbuffer, message);
	ASSERT(rWorkbuffer.Count<uint8_t>() == sizeof(uint8_t) + GameMessages::AssignPlayerMessage::kiSize);
	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void ServerSession::SendPlayerState(int64_t iClientId, PlayerStateWireType eWireType, int64_t iGlobalPlayerId, engine::GridCoord coord)
{
	engine::ClientConnection* pClient = engine::gpServer->FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	const GameMessages::PlayerStateDescriptor& rDescriptor = GameMessages::GetPlayerStateDescriptor(eWireType);
	LOG(kNetwork, kInfo, "ServerSession::SendPlayerState State: {} Client: {} GlobalPlayer: {} Grid: ({},{})", rDescriptor.pcName, iClientId, iGlobalPlayerId, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kServerPlayerState));
	GameMessages::PlayerStateMessage message {.uiWireType = static_cast<uint8_t>(eWireType), .iGlobalPlayerId = iGlobalPlayerId, .coord = coord};
	engine::NetworkMessages::Write(rWorkbuffer, message);
	ASSERT(rWorkbuffer.Count<uint8_t>() == sizeof(uint8_t) + GameMessages::PlayerStateMessage::kiSize);
	engine::NetworkManager::SendPacket(pClient->pPeer, engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
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

	for (engine::ClientConnection& rClient : engine::gpServer->mClients)
	{
		if (!rClient.bHandshakeComplete)
		{
			continue;
		}
		// [1B type][8B multiply][8B divide]
		engine::gpServer->SendSimplePacket(rClient.pPeer, GamePacketType::kServerTimespeedUpdate, engine::NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, iMultiply, iDivide);
	}
}

void ServerSession::StepTimescale(bool bFaster)
{
	if (bFaster)
	{
		gpGame->mTimeStep.IncreaseTimeScale();
	}
	else
	{
		gpGame->mTimeStep.DecreaseTimeScale();
	}
	BroadcastTimespeedIfChanged();
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

void ServerSession::SendNewSubscriptionFullStates()
{
	std::vector<engine::PendingNewSubscription>& rNewSubscriptions = mpRuntime->mpServer->mPendingNewSubscriptions;
	for (const engine::PendingNewSubscription& rSubscription : rNewSubscriptions)
	{
		const engine::ClientConnection* pClient = engine::gpServer->FindClient(rSubscription.iClientId);
		bool bSlotStillValid = (pClient != nullptr
			&& rSubscription.iSlot < std::ssize(pClient->slots)
			&& (pClient->slots.at(rSubscription.iSlot).subscription.flags & engine::SubscriptionFlags::kActive)
			&& pClient->slots.at(rSubscription.iSlot).subscription.coord == rSubscription.coord);
		if (!bSlotStillValid)
		{
			continue;
		}

		auto it = gpGame->mCoordFrames.find(rSubscription.coord);
		if (it != gpGame->mCoordFrames.end())
		{
			mpRuntime->mpServer->SendCoordStaticData(rSubscription.iClientId, rSubscription.iSlot, rSubscription.coord, it->second.staticData);
			mpRuntime->mpServer->SendCoordFullState(rSubscription.iClientId, rSubscription.iSlot, gpGame->TickCounter(), rSubscription.coord, it->second.pCurrent.get());
		}
	}

	// Persist-until-served: Server::Poll leaves this queue intact, so clear it here once serviced. A subscribe
	// accepted while the server is paused (iFullTicks == 0) stays queued across polls until it is serviced —
	// either by the next tick's ServerSessionRuntime::CompleteTick or, while still paused, by
	// ServerSessionRuntime::CompleteUpdate from GameBase::ServerUpdate — and its full state is sent.
	rNewSubscriptions.clear();
}

void ServerSession::HandleResyncRequests()
{
	std::vector<int64_t>& rResyncClientIds = mpRuntime->mpServer->mPendingResyncClientIds;
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

		LOG(kNetwork, kWarning, "ServerSession::HandleResyncRequests Client: {}", iClientId);

		for (int64_t iSlot = 0; iSlot < std::ssize(pClient->slots); ++iSlot)
		{
			if (!(pClient->slots.at(iSlot).subscription.flags & engine::SubscriptionFlags::kActive))
			{
				continue;
			}

			engine::GridCoord coord = pClient->slots.at(iSlot).subscription.coord;
			auto frameIt = gpGame->mCoordFrames.find(coord);
			if (frameIt == gpGame->mCoordFrames.end())
			{
				continue;
			}

			mpRuntime->mpServer->SendCoordFullState(iClientId, iSlot, gpGame->TickCounter(), coord, frameIt->second.pCurrent.get());
		}
	}

	// Persist-until-served: this queue is cleared here (not in Server::Poll) once the resync requests
	// are serviced so a request received while the server is paused survives across polls until the next tick.
	rResyncClientIds.clear();
}

void ServerSession::ResetClientsForLoad()
{
	LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad");
	// Heap: re-link rebuilds registry entries and authorizedCoords; pending state cleared across managers
	ScopedSuppressAllocationTracking suppress;

	mpRuntime->mpServer->BroadcastLoadNotification();

	mpFleetManager->mNavigation.ClearPendingFlagshipUpdates();
	std::vector<engine::ClientConnection>& rClients = engine::gpServer->mClients;

	// Try to re-link each client to their players by GUID
	for (engine::ClientConnection& rClient : rClients)
	{
		// Free all subscription slots
		for (int64_t i = 0; i < std::ssize(rClient.slots); ++i)
		{
			if (rClient.slots.at(i).subscription.flags & engine::SubscriptionFlags::kActive)
			{
				rClient.FreeSlot(i);
			}
		}

		// Clear owned players and rebuild from loaded frames.
		mClientPlayers.Clear(rClient.iClientId);
		if (mClientPlayers.RelinkFromFrames(rClient.iClientId, rClient.clientGuid, ClientPlayerRegistry::RelinkContext::kLoad) == 0)
		{
			LOG(kDefault, kDebug, "ServerSession::ResetClientsForLoad Client: {} no GUID match, will respawn", rClient.iClientId);
		}

		mpFleetManager->OnResetForLoad(rClient.iClientId, rClient.clientGuid);
	}

	// Clear all pending state across managers
	mpClientManager->ResetState();
	mpTransferManager->ResetState();
	mpBroadcaster->ResetState();
	// Fleet manager: only drop pending request queues. mFleets / mGuidToClientId
	// were just authoritatively restored by ReadFleetData + per-client OnResetForLoad above;
	// a full ResetState() here would annihilate that restoration.
	mpFleetManager->ClearPendingRequests();
	// Pending flagship updates were cleared at the start of this function via mNavigation.ClearPendingFlagshipUpdates(); fleet restoration above re-queued entries — do NOT clear again here.

	mpRuntime->ResetTransportForLoad();
}

void ServerSession::WriteFleetData(std::fstream& rFileStream) const
{
	::game::WriteFleetData(rFileStream, mpFleetManager->mFleets, mpFleetManager->mRandomEngine);
}

#endif // BT_SERVER

} // namespace game
