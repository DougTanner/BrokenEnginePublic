#include "Game.h"

#include "Network/ClientSession.h"
#include "Network/PlayerEvents.h"
#include "Profile/ProfileManager.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Spaceships/Spaceships.h"

namespace game
{

#if defined(BT_CLIENT)

ClientSession::ClientSession()
{
	gpClientSession = this;
	miCoordSlots = kiDesiredCoordSlots;
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

	if (mbClockErrorDisconnect)
	{
		snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Disconnected: clock error");
		mpClientNetwork->Disconnect();
	}

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

	// Parse and process player events from raw game packets
	std::vector<ReceivedPlayerEvent> playerEvents;
	ParsePlayerEvents(mpClientNetwork->DrainReceivedGamePackets(), playerEvents);
	for (const ReceivedPlayerEvent& rEvent : playerEvents)
	{
		switch (rEvent.eType)
		{
			case PlayerEventType::kAssigned:
				if (rEvent.playerId != gpGame->HumanPlayerId())
				{
					gpGame->SetHumanPlayerId(rEvent.playerId);
					gpGame->mHumanGridCoord = rEvent.coord;
					gpGame->mGameFlags.Clear(engine::GameFlags::kDeathScreen);
					UpdateSubscriptions();
				}
				break;
			case PlayerEventType::kSpawned:
			case PlayerEventType::kChangedFrame:
				gpGame->mHumanGridCoord = rEvent.coord;
				break;
			case PlayerEventType::kDied:
				gpGame->mGameFlags.Set(engine::GameFlags::kDeathScreen);
				if (gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord))
				{
					gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
				}
				gpGame->SetHumanPlayerId({});
				gpGame->SetPreviousHumanArmor(0.0f);
				break;
		}
	}

	ApplyReceivedFullStates();
	UpdateSubscriptions();
	ApplyReceivedUpdates();
}

void ClientSession::WaitForReconcile()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	ReconcileDesyncInfo desyncInfo = mpReconciler->Wait();
	if (desyncInfo.bDesync)
	{
		// Heap: Network sends for desync reporting
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		mpClientNetwork->SendDesyncReport(desyncInfo.iDesyncTick, desyncInfo.desyncCoord, desyncInfo.desyncServerCrc, desyncInfo.desyncClientCrc);
		mpClientNetwork->SendDebugFrameRequest(desyncInfo.iDesyncTick, desyncInfo.desyncCoord);
		mpClientNetwork->SetDesyncDebugMode(true);

		mDesyncDebugState.iTick = desyncInfo.iDesyncTick;
		mDesyncDebugState.coord = desyncInfo.desyncCoord;
		mDesyncDebugState.pClientFrame = std::move(desyncInfo.pDesyncClientFrame);
		mDesyncDebugState.entryTime = std::chrono::steady_clock::now();
	}
}

void ClientSession::TryKickReconcile()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	if (IsStalled())
	{
		return;
	}

	mpReconciler->TryKick();
}

void ClientSession::Poll()
{
	// Poll network and send ACK before reconciliation so server gets acknowledgement ASAP
	{
		// Heap: ENet polling
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		PollNetwork();
	}

	gpProfileManager->CpuStart(engine::kCpuTimerNetworkSend);
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
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
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		int64_t iPreReconcileTick = gpGame->TickCounter();
		WaitForReconcile();
		// Compensate time step for ticks rolled back during reconciliation
		int64_t iTickDeficit = iPreReconcileTick - gpGame->TickCounter();
		std::chrono::nanoseconds clockCorrectionNs = ComputeClockCorrectionNs(iPreReconcileTick);
		Log(kLogNetwork, "reconcileDeficit: {} clockCorrectionNs: {} remainderNs: {}", iTickDeficit, clockCorrectionNs.count(), gpGame->mTimeStep.mTickRemainderNs.count()); // DT: TEMP
		if (iTickDeficit > 0)
		{
			gpGame->mTimeStep.mTickRemainderNs += iTickDeficit * kTickNs;
		}
		gpGame->mTimeStep.mTickRemainderNs += clockCorrectionNs;
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkPollReconcile, true);
}

void ClientSession::PostRender()
{
	if (IsStalled())
	{
		return;
	}

	// Kick reconcile after render so snapshots remain valid for GetSnapshotFrame during rendering
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	TryKickReconcile();
}

void ClientSession::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedCoordFullState>& rFullStates = mpClientNetwork->DrainReceivedFullStates();
	if (rFullStates.empty())
	{
		return;
	}

	for (engine::ReceivedCoordFullState& rFullState : rFullStates)
	{
		engine::GridCoord coord = rFullState.coord;
		int64_t iTick = rFullState.iTick;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		engine::CoordFrames& rSub = gpGame->mCoordFrames.try_emplace(coord).first->second;
		if (rSub.uiGeneration == 0)
		{
			rSub.uiGeneration = mpReconciler->NextGeneration();
		}

		if (rSub.iConfirmedTick < 0)
		{
			// Single serialization copy: received -> current
			rSub.pCurrent = std::make_unique<Frame>();
			std::ostringstream outputStream;
			outputStream << *rFullState.pFrame;
			std::istringstream inputStream(outputStream.str());
			inputStream >> *rSub.pCurrent;

			if (rSub.pNext == nullptr)
			{
				rSub.pNext = std::make_unique<Frame>();
			}

			// Original received frame -> snapshot[0], which IS the confirmed frame
			rSub.iSnapshotHead = 0;
			rSub.snapshots[0] = std::move(rFullState.pFrame);
			rSub.snapshots[0]->postRender.serverCrc = rSub.snapshots[0]->ServerCrc();
			rSub.snapshots[0]->postRender.crc = rSub.snapshots[0]->Crc();
			rSub.iSnapshotCount = 1;
			rSub.iConfirmedTick = iTick;
			rSub.iConfirmedOffset = 0;

			// Set frame counter from first received full state
			if (gpGame->TickCounter() < iTick)
			{
				gpGame->SetTickCounter(iTick);
				gpGame->SetCurrentTime(rSub.pCurrent->interpolate.fCurrentTime);
			}

			ConfirmedHumanState confirmedState;
			confirmedState.humanGridCoord = gpGame->mHumanGridCoord;
			confirmedState.humanPlayerId = gpGame->HumanPlayerId();
			confirmedState.fPreviousHumanArmor = gpGame->PreviousHumanArmor();
			confirmedState.fCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			mpReconciler->InitConfirmedHumanState(confirmedState);
			mpReconciler->SetHasNewData();
		}
		else
		{
			// Coord already has confirmed state: store as pending for reconcile injection
			rSub.pendingFullState = engine::CoordFrames::PendingFullState {
				.iTick = iTick,
				.pFrame = std::move(rFullState.pFrame),
			};

			mpReconciler->SetHasNewData();
		}
	}

	// Try subscribing to the next coord in the queue
	TrySubscribeNext();
}

void ClientSession::ApplyReceivedUpdates()
{
	if (ApplyReceivedUpdatesBase())
	{
		mpReconciler->SetHasNewData();
	}
}

void ClientSession::ConnectToServer(std::string_view serverAddress)
{
	gpGame->mModalMessage[0] = '\0';
	ClientSessionBase::ConnectToServer(serverAddress, engine::kuiDefaultPort, kiDesiredCoordSlots);
}

void ClientSession::DisconnectFromServer()
{
	// Heap: ClientNetwork destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpReconciler->Reset();
	DisconnectFromServerBase();
	mDesyncDebugState = {};
	miDesyncCount = 0;
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

void ClientSession::PollDebugFrameResponse()
{
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpClientNetwork->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iTick, mDesyncDebugState.coord);
		mDesyncDebugState = {};

		if constexpr (kbEnableDesyncRecovery)
		{
			if constexpr (kbEnableDebugBreak)
			{
				DEBUG_BREAK();
			}

			RecoverFromDesync();
		}
		else
		{
			ASSERT(false);
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
			mpClientNetwork->Disconnect();
		}
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

	PollDebugFrameResponse();

	// Timeout desync debug mode if server never responds
	if (mDesyncDebugState.iTick >= 0 && std::chrono::steady_clock::now() - mDesyncDebugState.entryTime > kDesyncDebugTimeout)
	{
		mDesyncDebugState = {};
		if constexpr (kbEnableDesyncRecovery)
		{
			Log(kLogNetwork, "ClientSession::PollConnection Desync debug mode timed out, recovering without debug frame");
			RecoverFromDesync();
		}
		else
		{
			Log(kLogNetwork, "ClientSession::PollConnection Desync debug mode timed out, disconnecting");
			ASSERT(false);
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server (debug frame timeout)");
			mpClientNetwork->Disconnect();
		}
	}

	if (mpClientNetwork->WasDisconnected())
	{
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = gpGame->mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return false;
	}

	// Waiting for debug frame response — skip normal processing
	if (mDesyncDebugState.iTick >= 0)
	{
		return false;
	}

	return true;
}

void ClientSession::RecoverFromDesync()
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	if (miDesyncCount > 0 && now - mFirstDesyncTime > kDesyncWindowDuration)
	{
		miDesyncCount = 0;
	}

	if (miDesyncCount == 0)
	{
		mFirstDesyncTime = now;
	}
	++miDesyncCount;

	Log(kLogNetwork, "ClientSession::RecoverFromDesync DesyncCount: {} / {}", miDesyncCount, kiMaxDesyncsBeforeDisconnect);

	if (miDesyncCount >= kiMaxDesyncsBeforeDisconnect)
	{
		Log(kLogNetwork, "ClientSession::RecoverFromDesync Escalating to disconnect");
		snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
		mpClientNetwork->Disconnect();
		return;
	}

	mpClientNetwork->SendResyncRequest();
	mpClientNetwork->SetDesyncDebugMode(false);
	ResetCoordStatesForResync();
}

void ClientSession::ResetCoordStatesForResync()
{
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}

	mpReconciler->Reset();
}

void ClientSession::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iTick, [[maybe_unused]] engine::GridCoord coord)
{
	rClientFrame.LogDifferences(rServerFrame);
}

#endif // BT_CLIENT

} // namespace game
