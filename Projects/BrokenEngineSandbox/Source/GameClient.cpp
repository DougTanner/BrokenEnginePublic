#include "Game.h"

#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

void Game::PrepareExtrapolationTick(const std::vector<engine::GridCoord>& rActiveCoords)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = mCoordFrames.find(rCoord);
		if (subIt == mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
		{
			continue;
		}
		engine::CoordFrames& rSub = subIt->second;
		int64_t iSnapshot = rSub.iSnapshotCount;
		if (iSnapshot >= engine::kiTickRate)
		{
			continue;
		}
		if (rSub.snapshots[iSnapshot] == nullptr)
		{
			rSub.snapshots[iSnapshot] = std::make_unique<Frame>();
		}
	}
}

void Game::BuildExtrapolationFrameRef(const engine::GridCoord& rCoord, Frame*& rpNext, Frame*& rpCurrent)
{
	auto subIt = mCoordFrames.find(rCoord);
	if (subIt == mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
	{
		return;
	}
	engine::CoordFrames& rSub = subIt->second;
	int64_t iSnapshot = rSub.iSnapshotCount;
	if (iSnapshot >= engine::kiTickRate)
	{
		return;
	}
	rpNext = rSub.snapshots[iSnapshot].get();
	rpCurrent = (iSnapshot == 0) ? &CurrentFrame(rCoord) : rSub.snapshots[iSnapshot - 1].get();
}

void Game::RecordExtrapolationSnapshot(const std::vector<engine::GridCoord>& rActiveCoords, [[maybe_unused]] int64_t iTick)
{
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = mCoordFrames.find(rCoord);
		if (subIt == mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
		{
			continue;
		}
		engine::CoordFrames& rSub = subIt->second;
		if (rSub.iSnapshotCount >= engine::kiTickRate)
		{
			continue;
		}
		rSub.iSnapshotCount++;
	}
}

Frame* Game::GetSnapshotFrame(engine::GridCoord coord) const
{
	auto subIt = mCoordFrames.find(coord);
	if (subIt == mCoordFrames.end() || subIt->second.iConfirmedTick < 0 || subIt->second.iSnapshotCount <= 0)
	{
		return nullptr;
	}
	return subIt->second.snapshots[subIt->second.iSnapshotCount - 1].get();
}

void Game::ConnectToServer(const char* pServerAddress)
{
	FILE_LOG(0, "ConnectToServer: connecting to {}", pServerAddress);
	mModalMessage[0] = '\0';
	// Heap: NetworkClient allocates ENet host and peer
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpNetworkClient = std::make_unique<engine::NetworkClient>(pServerAddress, engine::kuiDefaultPort, kiDesiredCoordSlots);
}

void Game::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<engine::NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

std::chrono::nanoseconds Game::ComputeClockCorrectionNs(int64_t iPreReconcileTick)
{
	if (miLatestServerTick < 0)
	{
		return 0ns;
	}

	int64_t iRttUs = mpNetworkClient->GetPipelineRttUs();

	// Target: client should be ceil(RTT/2 / frameTime) + 1 frames behind server
	static constexpr int64_t kiTickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(kTickNs).count();
	int64_t iTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + kiTickTimeUs - 1) / kiTickTimeUs + 1) : 1;

	// Offset: positive = client ahead, negative = client behind
	int64_t iOffset = iPreReconcileTick - miLatestServerTick;

	// Error: positive = too far ahead (slow down), negative = too far behind (speed up)
	int64_t iError = iOffset + iTargetBehind;
	miClockError = iError;

	// Proportional correction capped at ±4, scaled to 1/64th of a frame step per error unit
	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	std::chrono::nanoseconds correction(-iCorrectionSteps * kTickNs.count() / 64);

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
	miLatestServerTick = -1;
	mpNetworkClient.reset();
	// Reset client fields on all subscribed frames
	for (auto& [rCoord, rSub] : mCoordFrames)
	{
		rSub.iConfirmedTick = -1;
		rSub.iConfirmedSnapshotIndex = -1;
		rSub.iSnapshotCount = 0;
		rSub.serverUpdates.clear();
		rSub.pendingFullState.reset();
		rSub.uiGeneration = 0;
	}
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
	if (mCoordFrames.contains(mHumanGridCoord) && CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu)
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
		FILE_LOG(0, "[PollNetworkClient] Debug frame received for frame={} coord=({},{}), running CompareWithServerFrame", mDesyncDebugState.iTick, mDesyncDebugState.coord.x, mDesyncDebugState.coord.y);
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iTick, mDesyncDebugState.coord);
		mDesyncDebugState = {};
		DEBUG_BREAK();
		snprintf(mModalMessage, sizeof(mModalMessage), "Desynced from server");
		mpNetworkClient->Disconnect();
		return;
	}

	if (mpNetworkClient->WasDisconnected())
	{
		FILE_LOG(0, "[PollNetworkClient] Disconnected while waiting for debug frame: desyncFrame={}", mDesyncDebugState.iTick);
		ChangeFrame(GameFlags::kMainMenu);
		meUiState = mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return;
	}

	// Waiting for debug frame response — skip normal processing
	if (mDesyncDebugState.iTick >= 0)
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

			FILE_LOG(0, "[PollNetworkClient] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), mHumanPlayerId.ToUuid().Value(), mHumanGridCoord.x, mHumanGridCoord.y, miTickCounter);

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
			if (mCoordFrames.contains(mHumanGridCoord))
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
		int64_t iTick = rFullState.iTick;

		// Initialize client-only objects
		Frame& rFrame = *rFullState.pFrame;
		BlastersInterpolate::ClientInitAll(rFrame);
		MissilesInterpolate::ClientInitAll(rFrame);
		SpaceshipsInterpolate::ClientInitAll(rFrame);

		engine::CoordFrames& rSub = mCoordFrames[coord];
		if (rSub.uiGeneration == 0)
		{
			rSub.uiGeneration = muiNextReconcileGeneration++;
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
			rSub.snapshots[0] = std::move(rFullState.pFrame);
			rSub.snapshots[0]->postRender.serverCrc = rSub.snapshots[0]->ServerCrc();
			rSub.snapshots[0]->postRender.crc = rSub.snapshots[0]->Crc();
			rSub.iSnapshotCount = 1;
			rSub.iConfirmedTick = iTick;
			rSub.iConfirmedSnapshotIndex = 0;

			// Set frame counter from first received full state
			if (miTickCounter < iTick)
			{
				miTickCounter = iTick;
				mfCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			}

			mConfirmedHumanState.humanGridCoord = mHumanGridCoord;
			mConfirmedHumanState.humanPlayerId = mHumanPlayerId;
			mConfirmedHumanState.fPreviousHumanArmor = mfPreviousHumanArmor;
			// Only set confirmed time from the first coord's full state;
			// later coords arrive at higher frame numbers and would desync the
			// confirmed time vs. iMinConfirmedFrame during reconciliation rollback
			if (mConfirmedHumanState.fCurrentTime == 0.0f)
			{
				mConfirmedHumanState.fCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			}

			mbReconcileHasNewData = true;
			FILE_LOG(0, "[ApplyReceivedFullStates] Initial coord=({},{}) tick={}", coord.x, coord.y, iTick);
		}
		else
		{
			// Coord already has confirmed state: store as pending for reconcile injection
			rSub.pendingFullState = engine::CoordFrames::PendingFullState {
				.iTick = iTick,
				.pFrame = std::move(rFullState.pFrame),
			};

			mbReconcileHasNewData = true;
			FILE_LOG(0, "[ApplyReceivedFullStates] Deferred coord=({},{}) tick={}", coord.x, coord.y, iTick);
		}
	}

	// Try subscribing to the next coord in the queue
	TrySubscribeNext();
}

void Game::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::vector<engine::ClientCoordSlot>& rCoordSlots = mpNetworkClient->GetCoordSlots();
	std::vector<std::vector<engine::ReceivedCoordUpdate>>& rAllUpdates = mpNetworkClient->DrainReceivedCoordUpdates();

	for (int64_t iSlot = 0; iSlot < std::ssize(rCoordSlots); ++iSlot)
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
		engine::CoordFrames& rSub = mCoordFrames[coord];

		for (engine::ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			// Skip frames at or before confirmed frame for this coord
			if (rUpdate.iTick <= rSub.iConfirmedTick)
			{
				continue;
			}

			if (static_cast<int64_t>(rSub.serverUpdates.size()) >= engine::kiMaxBufferedFrames)
			{
				continue;
			}

			miLatestServerTick = std::max(miLatestServerTick, rUpdate.iTick);

			if (!rSub.serverUpdates.contains(rUpdate.iTick))
			{
				rSub.serverUpdates[rUpdate.iTick] = {
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

int64_t Game::GetConfirmedTick() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rSub] : mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0 && (iMin < 0 || rSub.iConfirmedTick < iMin))
		{
			iMin = rSub.iConfirmedTick;
		}
	}
	return iMin;
}

int64_t Game::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rSub] : mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0)
		{
			iTotal += static_cast<int64_t>(rSub.serverUpdates.size());
		}
	}
	return iTotal;
}

void Game::UpdateSubscriptions()
{
	if (mpNetworkClient == nullptr)
	{
		return;
	}

	// Player alive but not yet in assigned cell (mid-transfer): maintain current subscriptions
	// Only guard when coord has confirmed server data — initial spawn must subscribe first
	if (mHumanPlayerId.IsValid() && mCoordFrames.contains(mHumanGridCoord)
		&& mCoordFrames[mHumanGridCoord].iConfirmedTick >= 0
		&& !CurrentFrame(mHumanGridCoord).interpolate.pPlayers->idToIndexMap.contains(mHumanPlayerId))
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Compute desired coords (priority-ordered)
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = !mHumanPlayerId.IsValid()
		&& mCoordFrames.contains(mHumanGridCoord)
		&& (CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen);

	if (mHumanPlayerId.IsValid())
	{
		desiredCoords.push_back(mHumanGridCoord);

		XMVECTOR vecArea = CurrentFrame(mHumanGridCoord).postRender.vecArea;
		XMVECTOR vecPos = GetHumanPlayerPosition();
		float fCenterX = (XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f;
		float fCenterY = (XMVectorGetY(vecArea) + XMVectorGetW(vecArea)) * 0.5f;

		engine::GridCoord quadrantOffsets[3];
		engine::ComputeQuadrantOffsets(XMVectorGetX(vecPos), XMVectorGetY(vecPos), fCenterX, fCenterY, quadrantOffsets);

		for (const engine::GridCoord& rOffset : quadrantOffsets)
		{
			engine::GridCoord neighbor {mHumanGridCoord.x + rOffset.x, mHumanGridCoord.y + rOffset.y};
			desiredCoords.push_back(neighbor);
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

	std::vector<engine::ClientCoordSlot>& rSlots = mpNetworkClient->GetCoordSlots();

	// Unsubscribe from coords no longer desired
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
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
			// Reset client fields on unsubscribed coord
			auto unsubIt = mCoordFrames.find(unsubCoord);
			if (unsubIt != mCoordFrames.end())
			{
				engine::CoordFrames& rUnSub = unsubIt->second;
				rUnSub.iConfirmedTick = -1;
				rUnSub.iConfirmedSnapshotIndex = -1;
				rUnSub.iSnapshotCount = 0;
				rUnSub.serverUpdates.clear();
				rUnSub.pendingFullState.reset();
				rUnSub.uiGeneration = 0;
			}
			FILE_LOG(0, "[UpdateSubscriptions] Unsubscribe slot={} coord=({},{})", i, unsubCoord.x, unsubCoord.y);
		}
	}

	// Build subscription queue: desired coords not yet subscribed (in priority order)
	mSubscriptionQueue.clear();
	for (const engine::GridCoord& rCoord : desiredCoords)
	{
		bool bAlreadySubscribed = false;
		for (int64_t i = 0; i < std::ssize(rSlots); ++i)
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
	const std::vector<engine::ClientCoordSlot>& rSlots = mpNetworkClient->GetCoordSlots();
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
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
		std::chrono::nanoseconds semaphoreWaitNs = timer.GetDeltaNs(true);

		std::chrono::nanoseconds maxWait = std::chrono::nanoseconds(100ms);
		if (semaphoreWaitNs > maxWait)
		{
			FILE_LOG(0, "[WaitForReconcile] Wait exceeded threshold: waitNs={} maxNs={}", semaphoreWaitNs.count(), maxWait.count());
			DEBUG_BREAK();
		}

		ApplyReconcileResult();
		mbReconcileInFlight = false;
	}
}

void Game::TryKickReconcile()
{
	if (mpNetworkClient == nullptr || GetConfirmedTick() < 0)
	{
		return;
	}

	if (mDesyncDebugState.iTick >= 0)
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

void Game::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iTick, [[maybe_unused]] engine::GridCoord coord)
{
	// Suppress DEBUG_BREAK so the full comparison chain runs
	common::gbSuppressVerifyFrameBreak = true;
	rClientFrame.ServerCompare(rServerFrame);
	common::gbSuppressVerifyFrameBreak = false;
}

#endif // BT_CLIENT

} // namespace game
