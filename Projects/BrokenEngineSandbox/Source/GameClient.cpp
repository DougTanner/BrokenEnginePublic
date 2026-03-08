#include "Game.h"

#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Profile/ProfileManager.h"

namespace game
{

#ifdef BT_CLIENT

void Game::PrepareExtrapolationTick(const std::vector<engine::GridCoord>& rActiveCoords)
{
	ScopedSuppressAllocationTracking ssat;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end())
		{
			continue;
		}
		auto& rState = stateIt->second;
		int64_t N = rState.iSnapshotCount;
		if (N >= CoordReconcileState::kiMaxSnapshots)
		{
			continue;
		}
		if (N >= static_cast<int64_t>(rState.snapshots.size()))
		{
			rState.snapshots.resize(static_cast<size_t>(N + 1));
		}
		if (rState.snapshots[N].pFrame == nullptr)
		{
			rState.snapshots[N].pFrame = std::make_unique<Frame>();
		}
	}
}

void Game::BuildExtrapolationFrameRef(const engine::GridCoord& rCoord, Frame*& rpNext, Frame*& rpCurrent)
{
	auto stateIt = mCoordReconcileStates.find(rCoord);
	if (stateIt == mCoordReconcileStates.end())
	{
		return;
	}
	auto& rState = stateIt->second;
	int64_t N = rState.iSnapshotCount;
	if (N >= CoordReconcileState::kiMaxSnapshots)
	{
		return;
	}
	rpNext = rState.snapshots[N].pFrame.get();
	rpCurrent = (N == 0) ? &CurrentFrame(rCoord) : rState.snapshots[N - 1].pFrame.get();
}

void Game::RecordExtrapolationSnapshot(const std::vector<engine::GridCoord>& rActiveCoords, int64_t iFrame)
{
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end())
		{
			continue;
		}
		auto& rState = stateIt->second;
		if (rState.iSnapshotCount >= CoordReconcileState::kiMaxSnapshots)
		{
			continue;
		}
		auto& rSnapshot = rState.snapshots[rState.iSnapshotCount];
		rSnapshot.iFrame = iFrame;
		rSnapshot.crc = rSnapshot.pFrame->ServerCrc();
		auto inputIt = mFrameInputs.find(rCoord);
		if (inputIt != mFrameInputs.end())
		{
			rSnapshot.inputCrc = inputIt->second.ServerInputCrc();
		}
		rState.iSnapshotCount++;
	}
}

void Game::BorrowSnapshotFrames(std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>>& rCurrentFrames)
{
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end() || stateIt->second.iSnapshotCount <= 0)
		{
			continue;
		}
		auto& rState = stateIt->second;
		std::swap(rCurrentFrames[rCoord], rState.snapshots[rState.iSnapshotCount - 1].pFrame);
	}
}

void Game::RestoreSnapshotFrames(std::unordered_map<engine::GridCoord, std::unique_ptr<Frame>>& rCurrentFrames)
{
	for (const engine::GridCoord& rCoord : mActiveCoords)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end() || stateIt->second.iSnapshotCount <= 0)
		{
			continue;
		}
		auto& rState = stateIt->second;
		std::swap(rCurrentFrames[rCoord], rState.snapshots[rState.iSnapshotCount - 1].pFrame);
	}
}

void Game::ConnectToServer(const char* pServerAddress)
{
	FILE_LOG(0, "ConnectToServer: connecting to {}", pServerAddress);
	mModalMessage[0] = '\0';
	// Heap: NetworkClient allocates ENet host and peer
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpNetworkClient = std::make_unique<engine::NetworkClient>(pServerAddress, engine::kuiDefaultPort);
}

void Game::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<engine::NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

std::chrono::nanoseconds Game::ComputeClockCorrectionNs(int64_t iPreReconcileFrame)
{
	if (miLatestServerFrame < 0)
	{
		return 0ns;
	}

	int64_t iRttUs = mpNetworkClient->GetPipelineRttUs();

	// Target: client should be ceil(RTT/2 / frameTime) + 1 frames behind server
	static constexpr int64_t kiFrameTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(kUpdateStepNs).count();
	int64_t iTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + kiFrameTimeUs - 1) / kiFrameTimeUs + 1) : 1;

	// Offset: positive = client ahead, negative = client behind
	int64_t iOffset = iPreReconcileFrame - miLatestServerFrame;

	// Error: positive = too far ahead (slow down), negative = too far behind (speed up)
	int64_t iError = iOffset + iTargetBehind;
	miClockError = iError;

	// Proportional correction capped at ±4, scaled to 1/64th of a frame step per error unit
	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	std::chrono::nanoseconds correction(-iCorrectionSteps * kUpdateStepNs.count() / 64);

	gpProfileManager->SetClockCorrection(iOffset, iTargetBehind, iError);

	return correction;
}

void Game::DisconnectFromServer()
{
	// Heap: NetworkClient destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	if constexpr (kbEnableReconcileThread)
	{
		if (mbReconcileInFlight)
		{
			mpReconcileWorker->Wait();
			mbReconcileInFlight = false;
		}
		mpReconcileContext.reset();
	}

	mbReconcileHasNewData = false;
	miLatestServerFrame = -1;
	mpNetworkClient.reset();
	mCoordReconcileStates.clear();
	mConfirmedHumanState = {};
	mDesyncDebugState = {};
	mSubscriptionQueue.clear();
	miClockError = 0;
}

void Game::PollNetworkClient()
{
	// Poll LAN discovery scanner
	if (mpDiscoveryScanner != nullptr)
	{
		mpDiscoveryScanner->Poll();

		if (mpDiscoveryScanner->IsFound())
		{
			char pcAddress[16] {};
			snprintf(pcAddress, sizeof(pcAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
			FILE_LOG(0, "Discovery: found server at {}", pcAddress);
			mpDiscoveryScanner.reset();
			ConnectToServer(pcAddress);
		}
		else if (!mpDiscoveryScanner->IsScanning())
		{
			FILE_LOG(0, "Discovery: scan timed out, no server found");
			DEBUG_BREAK();
			mpDiscoveryScanner.reset();
		}
	}

	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Heap: ENet polling allocates packets, DrainReceived* moves vectors, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpNetworkClient->Poll();

	// Wait for server connection response before entering game
	if (!mpNetworkClient->IsConnectionAccepted())
	{
		const char* pRejection = mpNetworkClient->GetRejectionReason();
		if (pRejection != nullptr)
		{
			snprintf(mModalMessage, sizeof(mModalMessage), "%s", pRejection);
			DisconnectFromServer();
			meUiState = UiState::kModal;
			return;
		}

		if (mpNetworkClient->WasDisconnected())
		{
			snprintf(mModalMessage, sizeof(mModalMessage), "Connection failed");
			DisconnectFromServer();
			meUiState = UiState::kModal;
			return;
		}

		return;
	}

	// Connection accepted - transition to game mode (runs once)
	if (mCurrentFrames.contains(mHumanGridCoord) && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu)
	{
		miGameMusicIndex = 0;
		engine::gpAudioManager->PlayMusic(mGameMusicPlaylist.at(0));
		CreateNewFrame(GameFlags::kGame);
		Reset();
		meUiState = UiState::kNone;
	}

	// Check for debug frame response
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpNetworkClient->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		FILE_LOG(0, "[PollNetworkClient] Debug frame received for frame={} coord=({},{}), running CompareWithServerFrame", mDesyncDebugState.iFrame, mDesyncDebugState.coord.x, mDesyncDebugState.coord.y);
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iFrame, mDesyncDebugState.coord);
		mDesyncDebugState = {};
		DEBUG_BREAK();
		snprintf(mModalMessage, sizeof(mModalMessage), "Desynced from server");
		mpNetworkClient->Disconnect();
		return;
	}

	if (mpNetworkClient->WasDisconnected())
	{
		FILE_LOG(0, "[PollNetworkClient] Disconnected while waiting for debug frame: desyncFrame={}", mDesyncDebugState.iFrame);
		ChangeFrame(GameFlags::kMainMenu);
		meUiState = mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return;
	}

	// Waiting for debug frame response — skip normal processing
	if (mDesyncDebugState.iFrame >= 0)
	{
		return;
	}

	// Check for player assignments
	for (const engine::ReceivedAssignment& rAssignment : mpNetworkClient->DrainReceivedAssignments())
	{
		if (rAssignment.playerId != mHumanPlayerId)
		{
			player_t oldHumanPlayerId = mHumanPlayerId;
			mHumanPlayerId = rAssignment.playerId;
			mHumanGridCoord = rAssignment.coord;

			FILE_LOG(0, "[PollNetworkClient] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, miFrameCounter);

			// Trigger subscription updates for the new grid position
			UpdateSubscriptions();
		}
	}

	// Process server-authoritative player state notifications
	for (const engine::ReceivedPlayerState& rState : mpNetworkClient->DrainReceivedPlayerStates())
	{
		switch (rState.eType)
		{
		case engine::PlayerStateType::kSpawned:
		case engine::PlayerStateType::kChangedFrame:
			mHumanGridCoord = rState.coord;
			break;
		case engine::PlayerStateType::kDied:
			if (mCurrentFrames.contains(mHumanGridCoord))
			{
				CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
			}
			mHumanPlayerId = {};
			mfPreviousHumanArmor = 0.0f;
			break;
		}
	}

	ApplyReceivedFullStates();
	UpdateSubscriptions();
	ApplyReceivedUpdates();
}

void Game::ApplyReceivedFullStates()
{
	// Heap: Moving unique_ptr<Frame> into mCurrentFrames, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	std::vector<engine::ReceivedCoordFullState>& rFullStates = mpNetworkClient->DrainReceivedFullStates();
	if (rFullStates.empty())
	{
		return;
	}

	for (engine::ReceivedCoordFullState& rFullState : rFullStates)
	{
		engine::GridCoord coord = rFullState.coord;
		int64_t iFrame = rFullState.iFrame;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		bool bNewEntry = !mCoordReconcileStates.contains(coord);
		CoordReconcileState& rState = mCoordReconcileStates[coord];
		if (bNewEntry)
		{
			rState.uiGeneration = muiNextReconcileGeneration++;
		}

		if (rState.iConfirmedFrame < 0)
		{
			// First full state for this coord: inject directly into mCurrentFrames
			mCurrentFrames[coord] = std::make_unique<Frame>();

			// Copy received frame into confirmed frame via serialize/deserialize (one-time)
			std::ostringstream outputStream;
			outputStream << *rFullState.pFrame;
			std::string serialized = outputStream.str();
			std::istringstream inputStream(serialized);
			inputStream >> *mCurrentFrames[coord];

			if (!mNextFrames.contains(coord))
			{
				mNextFrames[coord] = std::make_unique<Frame>();
			}

			// Establish confirmed state with a copy of the frame
			rState.iConfirmedFrame = iFrame;
			rState.pConfirmedFrame = std::make_unique<Frame>();
			std::istringstream confirmStream(serialized);
			confirmStream >> *rState.pConfirmedFrame;

			// Initialize snapshot stack with the received frame as stack[0]
			rState.snapshots.resize(CoordReconcileState::kiMaxSnapshots);
			rState.snapshots[0].pFrame = std::move(rFullState.pFrame);
			rState.snapshots[0].iFrame = iFrame;
			rState.snapshots[0].crc = rState.snapshots[0].pFrame->ServerCrc();
			rState.iSnapshotCount = 1;

			// Set frame counter from first received full state
			if (miFrameCounter < iFrame)
			{
				miFrameCounter = iFrame;
				mfCurrentTime = mCurrentFrames[coord]->interpolate.fCurrentTime;
			}

			mConfirmedHumanState.humanGridCoord = mHumanGridCoord;
			mConfirmedHumanState.humanPlayerId = mHumanPlayerId;
			mConfirmedHumanState.fPreviousHumanArmor = mfPreviousHumanArmor;
			// Only set confirmed time from the first coord's full state;
			// later coords arrive at higher frame numbers and would desync the
			// confirmed time vs. iMinConfirmedFrame during reconciliation rollback
			if (mConfirmedHumanState.fCurrentTime == 0.0f)
			{
				mConfirmedHumanState.fCurrentTime = mCurrentFrames[coord]->interpolate.fCurrentTime;
			}

			mbReconcileHasNewData = true;
			FILE_LOG(0, "[ApplyReceivedFullStates] Initial coord=({},{}) frame={}", coord.x, coord.y, iFrame);
		}
		else
		{
			// Coord already has confirmed state: store as pending for reconcile injection
			rState.pendingFullState = CoordReconcileState::PendingFullState {
				.iFrame = iFrame,
				.pFrame = std::move(rFullState.pFrame),
			};

			mbReconcileHasNewData = true;
			FILE_LOG(0, "[ApplyReceivedFullStates] Deferred coord=({},{}) frame={}", coord.x, coord.y, iFrame);
		}
	}

	// Try subscribing to the next coord in the queue
	TrySubscribeNext();
}

void Game::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const auto& rCoordSlots = mpNetworkClient->GetCoordSlots();
	auto& rAllUpdates = mpNetworkClient->DrainReceivedCoordUpdates();

	for (int64_t iSlot = 0; iSlot < engine::NetworkManager::kiMaxCoordSlots; ++iSlot)
	{
		std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates[iSlot];
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const engine::ClientCoordSlot& rSlot = rCoordSlots[iSlot];
		if (rSlot.eState != engine::CoordSubscriptionState::kActive)
		{
			rSlotUpdates.clear();
			continue;
		}

		engine::GridCoord coord = rSlot.coord;
		CoordReconcileState& rState = mCoordReconcileStates[coord];

		for (engine::ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			// Skip frames at or before confirmed frame for this coord
			if (rUpdate.iFrame <= rState.iConfirmedFrame)
			{
				continue;
			}

			if (static_cast<int64_t>(rState.serverUpdates.size()) >= engine::kiMaxBufferedFrames)
			{
				continue;
			}

			miLatestServerFrame = std::max(miLatestServerFrame, rUpdate.iFrame);

			if (!rState.serverUpdates.contains(rUpdate.iFrame))
			{
				rState.serverUpdates[rUpdate.iFrame] = {
					.serverCrc = rUpdate.serverCrc,
					.inputCrc = rUpdate.inputCrc,
					.statusChanges = std::move(rUpdate.statusChanges),
				};
				mbReconcileHasNewData = true;
			}
		}

		rSlotUpdates.clear();
	}
}

int64_t Game::GetConfirmedFrame() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rState] : mCoordReconcileStates)
	{
		if (rState.iConfirmedFrame >= 0 && (iMin < 0 || rState.iConfirmedFrame < iMin))
		{
			iMin = rState.iConfirmedFrame;
		}
	}
	return iMin;
}

int64_t Game::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rState] : mCoordReconcileStates)
	{
		iTotal += static_cast<int64_t>(rState.serverUpdates.size());
	}
	return iTotal;
}

void Game::UpdateSubscriptions()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Compute desired coords (priority-ordered)
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(10);

	bool bDead = !mHumanPlayerId.IsValid()
		&& mCurrentFrames.contains(mHumanGridCoord)
		&& (CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen);

	if (mHumanPlayerId.IsValid())
	{
		// Alive: human coord + neighbors + origin
		desiredCoords.push_back(mHumanGridCoord);

		for (const engine::GridCoord& rOffset : engine::kNeighborOffsets)
		{
			engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
			desiredCoords.push_back(neighbor);
		}

		if (!std::ranges::contains(desiredCoords, engine::kOriginCoord))
		{
			desiredCoords.push_back(engine::kOriginCoord);
		}
	}
	else if (bDead)
	{
		// Dead: keep death coord + pre-emptive origin for respawn
		desiredCoords.push_back(mHumanGridCoord);
		if (mHumanGridCoord != engine::kOriginCoord)
		{
			desiredCoords.push_back(engine::kOriginCoord);
		}
	}
	else
	{
		// Not yet assigned: just origin
		desiredCoords.push_back(engine::kOriginCoord);
	}

	auto& rSlots = mpNetworkClient->GetCoordSlots();

	// Unsubscribe from coords no longer desired
	for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (rSlots[i].eState == engine::CoordSubscriptionState::kUnsubscribed ||
		    rSlots[i].eState == engine::CoordSubscriptionState::kUnsubscribing)
		{
			continue;
		}

		if (!std::ranges::contains(desiredCoords, rSlots[i].coord))
		{
			engine::GridCoord unsubCoord = rSlots[i].coord;
			if (rSlots[i].eState == engine::CoordSubscriptionState::kSubscribing)
			{
				rSlots[i] = {};
			}
			else
			{
				mpNetworkClient->SendUnsubscribe(i);
			}
			mCoordReconcileStates.erase(unsubCoord);
			FILE_LOG(0, "[UpdateSubscriptions] Unsubscribe slot={} coord=({},{})", i, unsubCoord.x, unsubCoord.y);
		}
	}

	// Build subscription queue: desired coords not yet subscribed (in priority order)
	mSubscriptionQueue.clear();
	for (const engine::GridCoord& rCoord : desiredCoords)
	{
		bool bAlreadySubscribed = false;
		for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
		{
			if (rSlots[i].coord == rCoord &&
			    rSlots[i].eState != engine::CoordSubscriptionState::kUnsubscribed &&
			    rSlots[i].eState != engine::CoordSubscriptionState::kUnsubscribing)
			{
				bAlreadySubscribed = true;
				break;
			}
		}

		if (!bAlreadySubscribed)
		{
			mSubscriptionQueue.push_back(rCoord);
		}
	}

	// Start subscribing
	TrySubscribeNext();
}

void Game::TrySubscribeNext()
{
	if (mpNetworkClient == nullptr || mSubscriptionQueue.empty())
	{
		return;
	}

	// Check if any slot is currently subscribing or waiting for full state
	const auto& rSlots = mpNetworkClient->GetCoordSlots();
	for (int64_t i = 0; i < engine::NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (rSlots[i].eState == engine::CoordSubscriptionState::kSubscribing ||
		    rSlots[i].eState == engine::CoordSubscriptionState::kWaitingFullState ||
		    rSlots[i].eState == engine::CoordSubscriptionState::kUnsubscribing)
		{
			return; // Wait for current subscription or unsubscription to complete
		}
	}

	engine::GridCoord coord = mSubscriptionQueue.front();
	mSubscriptionQueue.erase(mSubscriptionQueue.begin());

	mpNetworkClient->SendSubscribe(coord);
	FILE_LOG(0, "[TrySubscribeNext] Subscribe coord=({},{}) remaining={}", coord.x, coord.y, mSubscriptionQueue.size());
}

void Game::WaitForReconcile()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	if constexpr (kbEnableReconcileThread)
	{
		// Async: wait for worker result from previous tick
		if (!mbReconcileInFlight)
		{
			return;
		}

		common::Timer timer;
		mpReconcileWorker->Wait();

		std::chrono::nanoseconds waitNs = timer.GetDeltaNs();
		std::chrono::nanoseconds maxWait = std::chrono::nanoseconds(100ms);
		if (waitNs > maxWait)
		{
			FILE_LOG(0, "[WaitForReconcile] Wait exceeded threshold: waitNs={} maxNs={}", waitNs.count(), maxWait.count());
			DEBUG_BREAK();
		}

		ApplyReconcileResult();
		mbReconcileInFlight = false;
	}
}

void Game::TryKickReconcile()
{
	if (mpNetworkClient == nullptr || GetConfirmedFrame() < 0)
	{
		return;
	}

	if (mDesyncDebugState.iFrame >= 0)
	{
		return;
	}

	if (!mbReconcileHasNewData)
	{
		return;
	}

	KickReconcile();
	mbReconcileInFlight = true;
	mbReconcileHasNewData = false;
}

void Game::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iFrame, [[maybe_unused]] engine::GridCoord coord)
{
	// Suppress DEBUG_BREAK so the full comparison chain runs
	common::gbSuppressVerifyFrameBreak = true;
	rClientFrame.ServerCompare(rServerFrame);
	common::gbSuppressVerifyFrameBreak = false;
}

#endif // BT_CLIENT

} // namespace game
