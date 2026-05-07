#include "Network/Client/ClientSession.h"

#include "Fleet.h"
#include "Frame/Collections/Players/Players.h"
#include "Game.h"
#include "Network/GamePacketType.h"
#include "Network/PlayerEvents.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

const char* ToString(SubscriptionChangeReason eReason)
{
	switch (eReason)
	{
		case SubscriptionChangeReason::kAssigned:       return "kAssigned";
		case SubscriptionChangeReason::kSpawned:        return "kSpawned";
		case SubscriptionChangeReason::kChangedFrame:   return "kChangedFrame";
		case SubscriptionChangeReason::kDied:           return "kDied";
		case SubscriptionChangeReason::kFleetSync:      return "kFleetSync";
		case SubscriptionChangeReason::kPollTick:       return "kPollTick";
		case SubscriptionChangeReason::kFocusNextFleet: return "kFocusNextFleet";
		case SubscriptionChangeReason::kFocusPrevFleet: return "kFocusPrevFleet";
		case SubscriptionChangeReason::kSelectPlayer:   return "kSelectPlayer";
	}
	return "Unknown";
}

ClientSession::ClientSession()
{
	gpClientSession = this;
	miCoordSlots = kiDesiredCoordSlots;
	mpDataReceiver = std::make_unique<ClientDataReceiver>();
	mpDesyncManager = std::make_unique<ClientDesyncManager>();
	mpReconciler = std::make_unique<ClientReconciler>();
}

ClientSession::~ClientSession()
{
	gpClientSession = nullptr;
}

std::chrono::nanoseconds ClientSession::ComputeClockCorrectionNs(int64_t iPreReconcileTick)
{
	std::chrono::nanoseconds correction = ClientSessionBase::ComputeClockCorrectionNs(iPreReconcileTick, kTickNs);
	gpProfileManager->SetClockCorrection(miClockOffset, miClockTargetBehind, miClockError);

	return correction;
}

void ClientSession::PollNetwork()
{
	// Heap: ENet polling allocates packets, DrainReceived* moves vectors, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if (!PollConnection())
	{
		return;
	}

	if (mpClientNetwork->DrainLoadNotification())
	{
		ResetForServerLoad();
	}

	// Parse and process player events from raw game packets
	std::vector<ReceivedPlayerEvent> playerEvents;
	ParsePlayerEvents(mpClientNetwork->DrainReceivedGamePackets(), playerEvents);
	auto updatePlayerCoord = [](engine::global_id_t globalPlayerId, engine::GridCoord coord)
	{
		for (int64_t i = 0; i < gpGame->PlayerCount(); ++i)
		{
			if (gpGame->mClientPlayerIds.at(i) == globalPlayerId)
			{
				gpGame->mClientPlayerCoords.at(i) = coord;
				break;
			}
		}
	};

	for (const ReceivedPlayerEvent& rEvent : playerEvents)
	{
		switch (rEvent.eType)
		{
			case PlayerEventType::kAssigned:
				if (!gpGame->IsClientPlayer(rEvent.globalPlayerId))
				{
					LOG(kNetwork, kVerbose, "PlayerEvent kAssigned NewGlobalPlayerId: {} NewCoord: ({},{}) FocusedGlobalPlayerId: {} FocusedCoord: ({},{})", rEvent.globalPlayerId, rEvent.coord.x, rEvent.coord.y, gpGame->ClientPlayerId(), gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y);
					gpGame->AddClientPlayer(rEvent.globalPlayerId, rEvent.coord);
				}
				UpdateDesiredCoords(SubscriptionChangeReason::kAssigned);
				break;
			case PlayerEventType::kSpawned:
			{
				updatePlayerCoord(rEvent.globalPlayerId, rEvent.coord);
				if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
				{
					gpGame->SetClientGridCoord(rEvent.coord);
				}
				UpdateDesiredCoords(SubscriptionChangeReason::kSpawned);
				break;
			}
			case PlayerEventType::kChangedFrame:
			{
				updatePlayerCoord(rEvent.globalPlayerId, rEvent.coord);
				if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
				{
					gpGame->SetClientGridCoord(rEvent.coord);
				}
				UpdateDesiredCoords(SubscriptionChangeReason::kChangedFrame);
				break;
			}
			case PlayerEventType::kDied:
				gpGame->RemoveClientPlayer(rEvent.globalPlayerId);
				UpdateDesiredCoords(SubscriptionChangeReason::kDied);
				break;
		}
	}

	// Parse fleet sync from remaining game packets
	std::vector<Fleet> receivedFleets;
	ParseFleetSync(mpClientNetwork->DrainReceivedGamePackets(), receivedFleets);
	if (!receivedFleets.empty())
	{
		engine::GridCoord preFleetCoord = gpGame->mClientGridCoord;
		gpGame->SyncFleets(std::move(receivedFleets));
		if (gpGame->mClientGridCoord != preFleetCoord)
		{
			UpdateDesiredCoords(SubscriptionChangeReason::kFleetSync);
		}
	}

	mpDataReceiver->ApplyReceivedStaticData();
	mpDataReceiver->ApplyReceivedFullStates();
	UpdateSubscriptions();
	mpDataReceiver->ApplyReceivedUpdates();
}

void ClientSession::Poll()
{
	// Poll network and send ACK before reconciliation so server gets acknowledgement ASAP
	{
		// Heap: ENet polling
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		PollNetwork();
	}

	gpProfileManager->CpuStart(engine::kCpuTimerNetworkSend);
	{
		// Heap: SendAck constructs ACK packet, Flush emits queued ENet sends
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		if (mpClientNetwork != nullptr)
		{
			mpClientNetwork->SendAck();
			mpClientNetwork->Flush();
		}
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkSend, true);
}

void ClientSession::Reconcile()
{
	if (IsStalled())
	{
		return;
	}

	gpProfileManager->CpuStart(engine::kCpuTimerNetworkPollReconcile);
	{
		// Heap: reconciliation deserialization and map operations
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		int64_t iCurrentTick = gpGame->TickCounter();
		if (mpClientNetwork != nullptr && !IsStalled())
		{
			ReconcileDesyncInfo desyncInfo = mpReconciler->Run();
			if (desyncInfo.bDesync)
			{
				mpDesyncManager->OnDesyncDetected(std::move(desyncInfo));
			}
		}
		std::chrono::nanoseconds clockCorrectionNs = ComputeClockCorrectionNs(iCurrentTick);

		static constexpr int64_t kiClockSnapThreshold = 28;
		if (miLatestServerTick >= 0 && (mbClockErrorDisconnect || std::abs(miClockError) >= kiClockSnapThreshold))
		{
			// Snap tick counter to recover from extreme clock error. Sim runs BEHIND latestServerTick.
			// Clamp at 0 so a fresh post-load server (latestServerTick < currentTargetBehind)
			// doesn't drive the client tick negative.
			int64_t iSnapTick = std::max<int64_t>(0, miLatestServerTick - miCurrentTargetBehind);
			LOG(kNetwork, kWarning, "ClientSession::Reconcile Clock snap OldTick: {} NewTick: {} LatestServerTick: {} TargetBehind: {}", iCurrentTick, iSnapTick, miLatestServerTick, miCurrentTargetBehind);
			gpGame->SetTickCounter(iSnapTick);
			gpGame->mTimeStep.ClearAccumulator();
			gpGame->ResetRenderClock();
			mbClockErrorDisconnect = false;
			miConsecutiveClockErrorFrames = 0;
			miClockError = 0;
			miLatestServerTick = -1;
		}
		else
		{
			gpGame->mTimeStep.mTickRemainderNs += clockCorrectionNs;
		}
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkPollReconcile, true);
}

void ClientSession::ConnectToServer(std::string_view serverAddress)
{
	gpGame->mModalMessage[0] = '\0';
	ClientSessionBase::ConnectToServer(serverAddress, engine::kuiDefaultPort, kiDesiredCoordSlots);
}

void ClientSession::ConnectToDiscoveredServer()
{
	mbServerDiscovered = false;
	ConnectToServer(mcDiscoveredAddress);
}

void ClientSession::DisconnectFromServer()
{
	// Heap: ClientNetwork destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpReconciler->Reset();
	DisconnectFromServerBase();
	mpDesyncManager->Reset();
	mDesiredCoords.clear();
	mUnwantedTimestamps.clear();
}

bool ClientSession::PollConnectionStatus()
{
	if (!mpClientNetwork->IsConnectionAccepted())
	{
		const char* pRejection = mpClientNetwork->GetRejectionReason();
		if (pRejection != nullptr)
		{
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "%s", pRejection);
			DisconnectFromServer();
			gpGame->meUiState = UiState::kModal;
			return false;
		}

		if (mpClientNetwork->WasDisconnected())
		{
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Connection failed");
			DisconnectFromServer();
			gpGame->meUiState = UiState::kModal;
			return false;
		}

		return false;
	}
	return true;
}

void ClientSession::TryEnterGame()
{
	if (gpGame->InMainMenu())
	{
		gpGame->StartGameMusic();
		gpGame->CreateNewFrame(GameFlags::kGame);
		gpGame->mGameFlags.Clear(engine::GameFlags::kMainMenu);
		gpGame->Reset();
		gpGame->meUiState = UiState::kNone;
	}
}

bool ClientSession::PollConnection()
{
	PollLANDiscovery();

	if (mpClientNetwork == nullptr)
	{
		return false;
	}

	mpClientNetwork->Poll();

	if (!PollConnectionStatus())
	{
		return false;
	}

	TryEnterGame();

	mpDesyncManager->PollDebugFrameResponse();
	mpDesyncManager->PollDesyncTimeout();

	if (mpClientNetwork->WasDisconnected())
	{
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = gpGame->mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return false;
	}

	// Waiting for debug frame response — skip normal processing
	if (IsStalled())
	{
		return false;
	}

	return true;
}

void ClientSession::ResetForServerLoad()
{
	LOG(kDefault, kDebug, "ClientSession::ResetForServerLoad");

	// Reset tick counter and time step — server tick resets to the saved value
	gpGame->SetTickCounter(0);
	gpGame->mTimeStep.ClearAccumulator();
	gpGame->mTimeStep.mRealTime.Reset();
	gpGame->ResetRenderClock();

	// Reset clock correction state
	miLatestServerTick = -1;
	miClockError = 0;
	miCurrentTargetBehind = 0;
	mbClockErrorDisconnect = false;
	miConsecutiveClockErrorFrames = 0;

	// Clear player identity — server will reassign
	gpGame->mClientPlayerIds.clear();
	gpGame->mClientPlayerCoords.clear();
	gpGame->SetClientGridCoord({});
	gpGame->SetPreviousClientArmor(0.0f);
	gpGame->mVecVisualErrorOffset = {};
	gpGame->mWeaponModeToggle.Reset();
	gpGame->mNavigationDelayControl.Reset();

	// Clear fleet state — server will re-sync
	gpGame->mClientFleets.clear();
	gpGame->miFocusedFleetIndex = -1;
	gpGame->miFocusedPlayerInFleetIndex = -1;

	// Force-reset all client coord slots
	std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		rSlots.at(i) = {};
	}
	mpClientNetwork->GetCancelledSubscriptions().clear();

	// Clear local coord frames (stale pre-load data). Reset render-progress fields first
	// so that any entry re-emplaced by a racing packet in the same frame starts clean.
	for (auto& [rCoord, rCoordFrames] : gpGame->mCoordFrames)
	{
		rCoordFrames.ResetClientState();
	}
	gpGame->mCoordFrames.clear();

	// Reset reconciler, subscription, and desync state
	mpReconciler->Reset();
	ClearSubscriptionState();
	mpDesyncManager->Reset();

	// Clear stale coord data from this poll cycle (game packets preserved for assign processing)
	mpClientNetwork->DrainReceivedFullStates().clear();
	for (std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates : mpClientNetwork->DrainReceivedCoordUpdates())
	{
		rSlotUpdates.clear();
	}
}

void ClientSession::ClearSubscriptionState()
{
	mDesiredCoords.clear();
	mUnwantedTimestamps.clear();
	mSubscriptionQueue.clear();
}

void ClientSession::SendUpdatePlayerRequest(int64_t iGlobalPlayerId, bool bUseMissiles, float fNavigationDelay)
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientUpdatePlayerRequest));
	rWorkbuffer.PushBack<int64_t>(iGlobalPlayerId);
	rWorkbuffer.PushBack<uint8_t>(bUseMissiles ? 1 : 0);
	rWorkbuffer.PushBack<float>(fNavigationDelay);

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kVerbose, "ClientSession::SendUpdatePlayerRequest GlobalPlayer: {} Missiles: {} NavDelay: {}", iGlobalPlayerId, bUseMissiles, fNavigationDelay);
}

void ClientSession::SendCreateFleetRequest()
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientCreateFleetRequest));

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kDebug, "ClientSession::SendCreateFleetRequest");

}

void ClientSession::SendDeleteFleetRequest(int64_t iFleetIndex)
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientDeleteFleetRequest));
	rWorkbuffer.PushBack<int64_t>(iFleetIndex);

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kDebug, "ClientSession::SendDeleteFleetRequest Fleet: {}", iFleetIndex);

}

void ClientSession::SendSpawnIntoFleetRequest(int64_t iFleetIndex)
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientSpawnIntoFleetRequest));
	rWorkbuffer.PushBack<int64_t>(iFleetIndex);

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kDebug, "ClientSession::SendSpawnIntoFleetRequest Fleet: {}", iFleetIndex);

}

void ClientSession::SendRespawnInFleetRequest(int64_t iFleetIndex, int64_t iMemberIndex)
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientRespawnInFleetRequest));
	rWorkbuffer.PushBack<int64_t>(iFleetIndex);
	rWorkbuffer.PushBack<int64_t>(iMemberIndex);

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kDebug, "ClientSession::SendRespawnInFleetRequest Fleet: {} Member: {}", iFleetIndex, iMemberIndex);

}

void ClientSession::SendFleetNavigationDelayRequest(int64_t iFleetIndex, float fDelay)
{
	if (!CanSend())
	{
		return;
	}

	std::optional<common::LogTickScope> optionalTickScope;
	if (common::gpThreadLocal->miLogTickCounter < 0)
	{
		optionalTickScope.emplace(gpGame->TickCounter());
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(GamePacketType::kClientFleetNavigationDelay));
	rWorkbuffer.PushBack<int64_t>(iFleetIndex);
	rWorkbuffer.PushBack<float>(fDelay);

	engine::NetworkManager::SendPacket(mpClientNetwork->GetServerPeer(), engine::NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	LOG(kNetwork, kDebug, "ClientSession::SendFleetNavigationDelayRequest Fleet: {} Delay: {}", iFleetIndex, fDelay);
}

#endif // BT_CLIENT

} // namespace game
