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

	if (mpClientNetwork->DrainLoadNotification())
	{
		ResetForServerLoad();
	}

	// Parse and process player events from raw game packets
	std::vector<ReceivedPlayerEvent> playerEvents;
	ParsePlayerEvents(mpClientNetwork->DrainReceivedGamePackets(), playerEvents);
	engine::GridCoord preEventClientCoord = gpGame->mClientGridCoord;
	for (const ReceivedPlayerEvent& rEvent : playerEvents)
	{
		switch (rEvent.eType)
		{
			case PlayerEventType::kAssigned:
				Log(kLogNetwork, kVerbose, "PlayerEvent kAssigned NewGlobalPlayerId: {} NewCoord: ({},{}) OldGlobalPlayerId: {} OldCoord: ({},{})", rEvent.globalPlayerId.iValue, rEvent.coord.x, rEvent.coord.y, gpGame->ClientPlayerId().iValue, gpGame->mClientGridCoord.x, gpGame->mClientGridCoord.y); // DT TEMP
				if (!gpGame->IsClientPlayer(rEvent.globalPlayerId))
				{
					gpGame->AddClientPlayer(rEvent.globalPlayerId, rEvent.coord);
				}
				gpGame->mGameFlags.Clear(engine::GameFlags::kDeathScreen);
				UpdateDesiredCoords("kAssigned");
				break;
			case PlayerEventType::kSpawned:
			{
				// Find matching player by global ID and update coord
				for (int64_t i = 0; i < gpGame->PlayerCount(); ++i)
				{
					if (gpGame->mClientPlayerIds.at(i) == rEvent.globalPlayerId)
					{
						gpGame->mClientPlayerCoords.at(i) = rEvent.coord;
						break;
					}
				}
				if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
				{
					gpGame->mClientGridCoord = rEvent.coord;
				}
				UpdateDesiredCoords("kSpawned");
				break;
			}
			case PlayerEventType::kChangedFrame:
			{
				// Find matching player by global ID and update coord
				for (int64_t i = 0; i < gpGame->PlayerCount(); ++i)
				{
					if (gpGame->mClientPlayerIds.at(i) == rEvent.globalPlayerId)
					{
						gpGame->mClientPlayerCoords.at(i) = rEvent.coord;
						break;
					}
				}
				// Only update quadrant dirs if it's the focused player
				if (rEvent.globalPlayerId == gpGame->ClientPlayerId())
				{
					gpGame->mClientGridCoord = rEvent.coord;
					int32_t iDeltaX = rEvent.coord.x - preEventClientCoord.x;
					int32_t iDeltaY = rEvent.coord.y - preEventClientCoord.y;
					if (iDeltaX != 0)
					{
						gpGame->miQuadrantDirX = -iDeltaX;
					}
					if (iDeltaY != 0)
					{
						gpGame->miQuadrantDirY = -iDeltaY;
					}
				}
				Log(kLogNetwork, kVerbose, "kChangedFrame GlobalPlayer: {} NewCoord: ({},{}) QuadrantDir: ({},{})", rEvent.globalPlayerId.iValue, rEvent.coord.x, rEvent.coord.y, gpGame->miQuadrantDirX, gpGame->miQuadrantDirY); // DT TEMP
				const std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
				for (int64_t i = 0; i < std::ssize(rSlots); ++i)
				{
					Log(kLogNetwork, kVerbose, "  Slot {} State: {} Coord: ({},{})", i, static_cast<int>(rSlots[i].eState), rSlots[i].coord.x, rSlots[i].coord.y);
				}
				UpdateDesiredCoords("kChangedFrame");
				break;
			}
			case PlayerEventType::kDied:
				gpGame->RemoveClientPlayer(rEvent.globalPlayerId);
				if (gpGame->PlayerCount() == 0)
				{
					gpGame->mGameFlags.Set(engine::GameFlags::kDeathScreen);
					auto it = gpGame->mCoordFrames.find(gpGame->mClientGridCoord);
					if (it != gpGame->mCoordFrames.end() && it->second.pCurrent != nullptr)
					{
						gpGame->CurrentFrame(gpGame->mClientGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
					}
					gpGame->SetPreviousClientArmor(0.0f);
				}
				UpdateDesiredCoords("kDied");
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
		mpClientNetwork->SendDesyncReport(desyncInfo.iDesyncTick, desyncInfo.desyncCoord, desyncInfo.desyncExpectedCrc, desyncInfo.desyncActualCrc);
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
		if (iTickDeficit > 0)
		{
			gpGame->mTimeStep.mTickRemainderNs += iTickDeficit * kTickNs;
		}
		gpGame->mTimeStep.mTickRemainderNs += clockCorrectionNs;

		// Gradually raise accumulator cap when behind to allow catch-up
		// miClockError already subtracts TargetBehind, so high-latency modes are accounted for
		constexpr int64_t kiCatchUpErrorThreshold = 8;
		if (miClockError <= -kiCatchUpErrorThreshold)
		{
			int64_t iCatchUpTicks = std::max(-miClockError / 2, engine::TimeStep::kiMaxAccumulatorTicks);
			if (gpGame->mTimeStep.miCatchUpAccumulatorTicks != iCatchUpTicks)
			{
				Log(kLogNetwork, kVerbose, "ClientSession::Reconcile CatchUp accumulator Error: {} Cap: {}", miClockError, iCatchUpTicks);
			}
			gpGame->mTimeStep.miCatchUpAccumulatorTicks = iCatchUpTicks;
		}
		else if (gpGame->mTimeStep.miCatchUpAccumulatorTicks > 0)
		{
			Log(kLogNetwork, kVerbose, "ClientSession::Reconcile Restoring accumulator cap Error: {}", miClockError);
			gpGame->mTimeStep.miCatchUpAccumulatorTicks = 0;
		}
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkPollReconcile, true);

	UpdateDesiredCoords("tick");
	UpdateSubscriptions();
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

		engine::CoordFrames& rSub = gpGame->mCoordFrames.try_emplace(coord).first->second;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		// Copy smoke trail smoothed positions from existing frame to preserve rendering continuity across reconciliation
		if (rSub.pCurrent != nullptr)
		{
			const engine::SmokeTrailsInterpolate& rOldSmokeTrails = rSub.pCurrent->interpolate.smokeTrails;
			engine::SmokeTrailsInterpolate& rNewSmokeTrails = rFrame.interpolate.smokeTrails;
			int64_t iCopyCount = std::min(rOldSmokeTrails.iCount, rNewSmokeTrails.iCount);
			if (iCopyCount > 0)
			{
				std::memcpy(rNewSmokeTrails.pVecSmoothedPositions, rOldSmokeTrails.pVecSmoothedPositions, iCopyCount * sizeof(XMVECTOR));
			}
		}
		if (rSub.uiGeneration == 0)
		{
			rSub.uiGeneration = mpReconciler->NextGeneration();
		}

		if (rSub.iConfirmedTick < 0)
		{
			// Only advance tick counter during initial setup (no other coords have confirmed data yet)
			bool bInitialSetup = (GetConfirmedTick() < 0);

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
			auto [crc, sharedCrc] = rSub.snapshots[0]->Crcs();
			rSub.snapshots[0]->postRender.crc = crc;
			rSub.snapshots[0]->postRender.sharedCrc = sharedCrc;
			rSub.iSnapshotCount = 1;
			rSub.iConfirmedTick = iTick;
			rSub.iConfirmedOffset = 0;

			// Set frame counter from first received full state only (not from subsequent neighbor subscriptions)
			if (bInitialSetup && gpGame->TickCounter() < iTick)
			{
				gpGame->SetTickCounter(iTick);
				gpGame->SetCurrentTime(rSub.pCurrent->interpolate.fCurrentTime);
			}

			ConfirmedClientState confirmedState;
			confirmedState.clientGridCoord = gpGame->mClientGridCoord;
			confirmedState.clientGlobalPlayerId = gpGame->ClientPlayerId();
			confirmedState.fPreviousClientArmor = gpGame->PreviousClientArmor();
			confirmedState.fCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			mpReconciler->InitConfirmedClientState(confirmedState);
			mpReconciler->SetHasNewData();
		}
		else
		{
			// Reject stale full states: tick must be after confirmed tick
			if (iTick <= rSub.iConfirmedTick)
			{
				Log(kLogNetwork, kVerbose, "ApplyReceivedFullStates Rejected stale full state Coord: ({},{}) FullStateTick: {} ConfirmedTick: {}", coord.x, coord.y, iTick, rSub.iConfirmedTick);
				continue;
			}

			// Coord already has confirmed state: store as pending for reconcile injection
			rSub.pendingFullState = engine::CoordFrames::PendingFullState {
				.iTick = iTick,
				.pFrame = std::move(rFullState.pFrame),
			};

			mpReconciler->SetHasNewData();
		}
	}

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
	mDesyncDebugState = {};
	miDesyncCount = 0;
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

void ClientSession::PollDebugFrameResponse()
{
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpClientNetwork->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iTick, mDesyncDebugState.coord);
		mDesyncDebugState = {};

		if constexpr (kbDesyncRecovery)
		{
			if constexpr (kbDebugBreak)
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
		if constexpr (kbDesyncRecovery)
		{
			Log(kLogNetwork, kWarning, "ClientSession::PollConnection Desync debug mode timed out, recovering without debug frame");
			RecoverFromDesync();
		}
		else
		{
			Log(kLogNetwork, kWarning, "ClientSession::PollConnection Desync debug mode timed out, disconnecting");
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

	Log(kLogNetwork, kWarning, "ClientSession::RecoverFromDesync DesyncCount: {} / {}", miDesyncCount, kiMaxDesyncsBeforeDisconnect);

	if (miDesyncCount >= kiMaxDesyncsBeforeDisconnect)
	{
		Log(kLogNetwork, kWarning, "ClientSession::RecoverFromDesync Escalating to disconnect");
		snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
		mpClientNetwork->Disconnect();
		return;
	}

	mpClientNetwork->SendResyncRequest();
	mpClientNetwork->SetDesyncDebugMode(false);
	ResetCoordStatesForResync();
}

void ClientSession::ResetForServerLoad()
{
	Log("ClientSession::ResetForServerLoad");

	// Reset tick counter and time step — server tick resets to the saved value
	gpGame->SetTickCounter(0);
	gpGame->mTimeStep.ClearAccumulator();
	gpGame->mTimeStep.mRealTime.Reset();

	// Reset clock correction state
	miLatestServerTick = -1;
	miClockError = 0;
	miCurrentTargetBehind = 0;
	mbClockErrorDisconnect = false;
	miConsecutiveClockErrorFrames = 0;

	// Clear player identity — server will reassign
	gpGame->mClientPlayerIds.clear();
	gpGame->mClientPlayerCoords.clear();
	gpGame->miFocusedPlayerIndex = -1;
	gpGame->mClientGridCoord = {};
	gpGame->SetPreviousClientArmor(0.0f);
	gpGame->mGameFlags.Clear(engine::GameFlags::kDeathScreen);
	gpGame->mVecVisualErrorOffset = {};
	gpGame->mWeaponModeToggle.Reset();
	gpGame->mSpawnToggle.Reset();

	// Force-reset all client coord slots
	std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		rSlots.at(i) = {};
	}
	mpClientNetwork->GetCancelledSubscriptions().clear();

	// Clear local coord frames (stale pre-load data)
	gpGame->mCoordFrames.clear();

	// Reset reconciler and subscription state
	mpReconciler->Reset();
	mDesiredCoords.clear();
	mUnwantedTimestamps.clear();
	mSubscriptionQueue.clear();
	mDesyncDebugState = {};
	miDesyncCount = 0;

	// Clear stale coord data from this poll cycle (game packets preserved for assign processing)
	mpClientNetwork->DrainReceivedFullStates().clear();
	for (std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates : mpClientNetwork->DrainReceivedCoordUpdates())
	{
		rSlotUpdates.clear();
	}
}

void ClientSession::ResetCoordStatesForResync()
{
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}

	mpReconciler->Reset();
	mUnwantedTimestamps.clear();
}

void ClientSession::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iTick, [[maybe_unused]] engine::GridCoord coord)
{
	rClientFrame.LogDifferences(rServerFrame);
}

#endif // BT_CLIENT

} // namespace game
