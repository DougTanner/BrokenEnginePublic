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

void Game::StoreExtrapolatedSnapshot(int64_t iFrame)
{
	// Store per-coord extrapolated snapshots for CRC fast-path
	for (const auto& [rCoord, pFrame] : mCurrentFrames)
	{
		auto stateIt = mCoordReconcileStates.find(rCoord);
		if (stateIt == mCoordReconcileStates.end())
		{
			continue;
		}

		CoordExtrapolatedSnapshot& rSnapshot = stateIt->second.extrapolatedSnapshots[iFrame];
		rSnapshot.crc = pFrame->ServerCrc();
		auto inputIt = mFrameInputs.find(rCoord);
		if (inputIt != mFrameInputs.end())
		{
			rSnapshot.inputCrc = inputIt->second.ServerInputCrc();
		}
		std::ostringstream outputStream;
		outputStream << *pFrame;
		rSnapshot.serializedFrame = outputStream.str();
	}
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
	if (CurrentFrame(mHumanGridCoord).interpolate.gameFlags & GameFlags::kMainMenu)
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
			CurrentFrame(mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
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
			mCurrentFrames[coord] = std::move(rFullState.pFrame);

			if (!mNextFrames.contains(coord))
			{
				mNextFrames[coord] = std::make_unique<Frame>();
			}

			// Establish confirmed state for this coord
			rState.iConfirmedFrame = iFrame;
			std::ostringstream outputStream;
			outputStream << *mCurrentFrames[coord];
			rState.confirmedSerializedFrame = outputStream.str();

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
			std::ostringstream outputStream;
			outputStream << rFrame;
			rState.pendingFullState = {iFrame, outputStream.str()};

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
		std::chrono::nanoseconds maxWait = std::chrono::nanoseconds(1s) / 2; // DT: TEMP engine::gpGraphics->miMonitorRefreshRate;
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
	FILE_LOG(0, "[CompareWithServerFrame] frame={} coord=({},{})", iFrame, coord.x, coord.y);
	FILE_LOG(0, "[CompareWithServerFrame] CLIENT: pushers={}/{} spaceships={} missiles={} blasters={} players={} explosions={}", rClientFrame.interpolate.pushers.iCount, rClientFrame.interpolate.pushers.idToIndexMap.size(), rClientFrame.interpolate.pSpaceships->iCount, rClientFrame.interpolate.pMissiles->iCount, rClientFrame.interpolate.pBlasters->iCount, rClientFrame.interpolate.pPlayers->iCount, rClientFrame.interpolate.explosions.iCount);
	FILE_LOG(0, "[CompareWithServerFrame] SERVER: pushers={}/{} spaceships={} missiles={} blasters={} players={} explosions={}", rServerFrame.interpolate.pushers.iCount, rServerFrame.interpolate.pushers.idToIndexMap.size(), rServerFrame.interpolate.pSpaceships->iCount, rServerFrame.interpolate.pMissiles->iCount, rServerFrame.interpolate.pBlasters->iCount, rServerFrame.interpolate.pPlayers->iCount, rServerFrame.interpolate.explosions.iCount);

	// Suppress DEBUG_BREAK so the full comparison chain runs
	common::gbSuppressVerifyFrameBreak = true;
	rClientFrame.ServerCompare(rServerFrame);
	common::gbSuppressVerifyFrameBreak = false;

	// Random engine comparison
	FILE_LOG(0, "[Compare] randomEngine: client={} server={}", rClientFrame.postRender.randomEngine.Crc(), rServerFrame.postRender.randomEngine.Crc());

	// Frame timing comparison
	FILE_LOG(0, "[Compare] fCurrentTime: client={:.6f} server={:.6f}", rClientFrame.interpolate.fCurrentTime, rServerFrame.interpolate.fCurrentTime);
	FILE_LOG(0, "[Compare] fDeltaTime: client={:.6f} server={:.6f}", rClientFrame.interpolate.fDeltaTime, rServerFrame.interpolate.fDeltaTime);
	FILE_LOG(0, "[Compare] frameFlags: client={} server={}", std::to_underlying(rClientFrame.interpolate.frameFlags.meFlags), std::to_underlying(rServerFrame.interpolate.frameFlags.meFlags));
	FILE_LOG(0, "[Compare] gameFlags: client={} server={}", std::to_underlying(rClientFrame.interpolate.gameFlags.meFlags), std::to_underlying(rServerFrame.interpolate.gameFlags.meFlags));

	// Per-pusher mismatch diagnosis: identify owner entity for divergent pushers
	int64_t iPusherMin = std::min(rClientFrame.interpolate.pushers.iCount, rServerFrame.interpolate.pushers.iCount);
	for (int64_t i = 0; i < iPusherMin; ++i)
	{
		XMFLOAT4 f4Client, f4Server;
		XMStoreFloat4(&f4Client, rClientFrame.interpolate.pushers.pVecPositions[i]);
		XMStoreFloat4(&f4Server, rServerFrame.interpolate.pushers.pVecPositions[i]);
		if (std::memcmp(&f4Client, &f4Server, sizeof(XMFLOAT4)) != 0)
		{
			FILE_LOG(0, "[Compare] Pusher[{}] MISMATCH client=({:.4f},{:.4f},{:.4f}) server=({:.4f},{:.4f},{:.4f}) radius c={:.4f} s={:.4f}",
				i, f4Client.x, f4Client.y, f4Client.z, f4Server.x, f4Server.y, f4Server.z,
				rClientFrame.interpolate.pushers.pfRadii[i], rServerFrame.interpolate.pushers.pfRadii[i]);
		}
	}

	// Log all spaceship positions
	int64_t iSpaceshipMin = std::min(rClientFrame.interpolate.pSpaceships->iCount, rServerFrame.interpolate.pSpaceships->iCount);
	for (int64_t i = 0; i < iSpaceshipMin; ++i)
	{
		FILE_LOG(0, "[Compare] Spaceship[{}] client=({:.4f},{:.4f},{:.4f}) server=({:.4f},{:.4f},{:.4f})",
			i,
			XMVectorGetX(rClientFrame.interpolate.pSpaceships->pVecPositions[i]), XMVectorGetY(rClientFrame.interpolate.pSpaceships->pVecPositions[i]), XMVectorGetZ(rClientFrame.interpolate.pSpaceships->pVecPositions[i]),
			XMVectorGetX(rServerFrame.interpolate.pSpaceships->pVecPositions[i]), XMVectorGetY(rServerFrame.interpolate.pSpaceships->pVecPositions[i]), XMVectorGetZ(rServerFrame.interpolate.pSpaceships->pVecPositions[i]));
	}

	// Log all missile positions
	int64_t iMissileMin = std::min(rClientFrame.interpolate.pMissiles->iCount, rServerFrame.interpolate.pMissiles->iCount);
	for (int64_t i = 0; i < iMissileMin; ++i)
	{
		FILE_LOG(0, "[Compare] Missile[{}] client=({:.4f},{:.4f},{:.4f}) server=({:.4f},{:.4f},{:.4f})",
			i,
			XMVectorGetX(rClientFrame.interpolate.pMissiles->pVecPositions[i]), XMVectorGetY(rClientFrame.interpolate.pMissiles->pVecPositions[i]), XMVectorGetZ(rClientFrame.interpolate.pMissiles->pVecPositions[i]),
			XMVectorGetX(rServerFrame.interpolate.pMissiles->pVecPositions[i]), XMVectorGetY(rServerFrame.interpolate.pMissiles->pVecPositions[i]), XMVectorGetZ(rServerFrame.interpolate.pMissiles->pVecPositions[i]));
	}

	// Log all player positions
	int64_t iPlayerMin = std::min(rClientFrame.interpolate.pPlayers->iCount, rServerFrame.interpolate.pPlayers->iCount);
	for (int64_t i = 0; i < iPlayerMin; ++i)
	{
		FILE_LOG(0, "[Compare] Player[{}] client=({:.4f},{:.4f},{:.4f}) server=({:.4f},{:.4f},{:.4f})",
			i,
			XMVectorGetX(rClientFrame.interpolate.pPlayers->pVecPositions[i]), XMVectorGetY(rClientFrame.interpolate.pPlayers->pVecPositions[i]), XMVectorGetZ(rClientFrame.interpolate.pPlayers->pVecPositions[i]),
			XMVectorGetX(rServerFrame.interpolate.pPlayers->pVecPositions[i]), XMVectorGetY(rServerFrame.interpolate.pPlayers->pVecPositions[i]), XMVectorGetZ(rServerFrame.interpolate.pPlayers->pVecPositions[i]));
	}

	// Log sub-CRCs for targeted diagnosis
	FILE_LOG(0, "[Compare] InterpolateCrc: client={} server={}", FrameInterpolate::ServerCrc(rClientFrame.interpolate), FrameInterpolate::ServerCrc(rServerFrame.interpolate));
	FILE_LOG(0, "[Compare] PostRenderCrc: client={} server={}", FramePostRender::ServerCrc(rClientFrame.postRender), FramePostRender::ServerCrc(rServerFrame.postRender));

	// Per-field PostRender CRC breakdown (Base scalars)
	FILE_LOG(0, "[Compare] PR vecArea: client={} server={}", common::Crc(rClientFrame.postRender.vecArea), common::Crc(rServerFrame.postRender.vecArea));
	FILE_LOG(0, "[Compare] PR uiNextUuid: client={} server={}", common::Crc(rClientFrame.postRender.uiNextUuid), common::Crc(rServerFrame.postRender.uiNextUuid));
	FILE_LOG(0, "[Compare] PR uiFrameId: client={} server={}", common::Crc(rClientFrame.postRender.uiFrameId), common::Crc(rServerFrame.postRender.uiFrameId));
	FILE_LOG(0, "[Compare] PR eIslandsFlip: client={} server={}", common::Crc(rClientFrame.postRender.eIslandsFlip), common::Crc(rServerFrame.postRender.eIslandsFlip));
	FILE_LOG(0, "[Compare] PR alignments: client={} server={}", rClientFrame.postRender.alignments.Crc(), rServerFrame.postRender.alignments.Crc());

	// Per-field PostRender CRC breakdown (Base collections)
	FILE_LOG(0, "[Compare] PR Explosions: client={} server={}", engine::ServerCollectionCrc(rClientFrame.postRender.explosions), engine::ServerCollectionCrc(rServerFrame.postRender.explosions));
	FILE_LOG(0, "[Compare] PR Pushers: client={} server={}", engine::ServerCollectionCrc(rClientFrame.postRender.pushers), engine::ServerCollectionCrc(rServerFrame.postRender.pushers));

	// Per-field PostRender CRC breakdown (Game scalars)
	FILE_LOG(0, "[Compare] PR enemyAlignment: client={} server={}", common::Crc(rClientFrame.postRender.enemyAlignment), common::Crc(rServerFrame.postRender.enemyAlignment));
	FILE_LOG(0, "[Compare] PR playerAlignment: client={} server={}", common::Crc(rClientFrame.postRender.playerAlignment), common::Crc(rServerFrame.postRender.playerAlignment));

	// Per-field PostRender CRC breakdown (Game collections)
	FILE_LOG(0, "[Compare] PR Players: client={} server={}", engine::CollectionCrc(*rClientFrame.postRender.pPlayers, rClientFrame.postRender.pPlayers->Members()), engine::CollectionCrc(*rServerFrame.postRender.pPlayers, rServerFrame.postRender.pPlayers->Members()));

	// Phase 3: Players PostRender field isolation
	const auto* pClientPlayers = rClientFrame.postRender.pPlayers.get();
	const auto* pServerPlayers = rServerFrame.postRender.pPlayers.get();
	FILE_LOG(0, "[Compare] PR Players count: client={} server={}", pClientPlayers->iCount, pServerPlayers->iCount);

	int64_t iPlayersMin = std::min(pClientPlayers->iCount, pServerPlayers->iCount);
	for (int64_t i = 0; i < iPlayersMin; ++i)
	{
		common::crc_t clientElemCrc = engine::MultiElementCrc(i, pClientPlayers->Members());
		common::crc_t serverElemCrc = engine::MultiElementCrc(i, pServerPlayers->Members());
		FILE_LOG(0, "[Compare] PR Players[{}] elem: client={} server={} {}", i, clientElemCrc, serverElemCrc, clientElemCrc == serverElemCrc ? "MATCH" : "MISMATCH");

		if (clientElemCrc != serverElemCrc)
		{
			FILE_LOG(0, "[Compare] PR Players[{}] puiIds: client={} server={}", i, common::Crc(pClientPlayers->puiIds[i]), common::Crc(pServerPlayers->puiIds[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pFlags: client={} server={}", i, common::Crc(pClientPlayers->pFlags[i]), common::Crc(pServerPlayers->pFlags[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pAlignments: client={} server={}", i, common::Crc(pClientPlayers->pAlignments[i]), common::Crc(pServerPlayers->pAlignments[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfNextBlasterFireTimes: client={} server={}", i, common::Crc(pClientPlayers->pfNextBlasterFireTimes[i]), common::Crc(pServerPlayers->pfNextBlasterFireTimes[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfNextSecondarySpawnTimes: client={} server={}", i, common::Crc(pClientPlayers->pfNextSecondarySpawnTimes[i]), common::Crc(pServerPlayers->pfNextSecondarySpawnTimes[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfNextSecondarySpawnTimes VALUE: client={:.6f} server={:.6f} diff={:.6f}", i, pClientPlayers->pfNextSecondarySpawnTimes[i], pServerPlayers->pfNextSecondarySpawnTimes[i], pClientPlayers->pfNextSecondarySpawnTimes[i] - pServerPlayers->pfNextSecondarySpawnTimes[i]);
			FILE_LOG(0, "[Compare] PR Players[{}] pVecVelocities: client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i,
				XMVectorGetX(pClientPlayers->pVecVelocities[i]), XMVectorGetY(pClientPlayers->pVecVelocities[i]), XMVectorGetZ(pClientPlayers->pVecVelocities[i]),
				XMVectorGetX(pServerPlayers->pVecVelocities[i]), XMVectorGetY(pServerPlayers->pVecVelocities[i]), XMVectorGetZ(pServerPlayers->pVecVelocities[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pVecVelocities CRC: client={} server={}", i, common::Crc(pClientPlayers->pVecVelocities[i]), common::Crc(pServerPlayers->pVecVelocities[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pVecWantedDirections: client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i,
				XMVectorGetX(pClientPlayers->pVecWantedDirections[i]), XMVectorGetY(pClientPlayers->pVecWantedDirections[i]), XMVectorGetZ(pClientPlayers->pVecWantedDirections[i]),
				XMVectorGetX(pServerPlayers->pVecWantedDirections[i]), XMVectorGetY(pServerPlayers->pVecWantedDirections[i]), XMVectorGetZ(pServerPlayers->pVecWantedDirections[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pVecWantedDirections CRC: client={} server={}", i, common::Crc(pClientPlayers->pVecWantedDirections[i]), common::Crc(pServerPlayers->pVecWantedDirections[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfArmors: client={} server={}", i, common::Crc(pClientPlayers->pfArmors[i]), common::Crc(pServerPlayers->pfArmors[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfShields: client={} server={}", i, common::Crc(pClientPlayers->pfShields[i]), common::Crc(pServerPlayers->pfShields[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfShieldCooldowns: client={} server={}", i, common::Crc(pClientPlayers->pfShieldCooldowns[i]), common::Crc(pServerPlayers->pfShieldCooldowns[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfDestroyedExplosionTimes: client={} server={}", i, common::Crc(pClientPlayers->pfDestroyedExplosionTimes[i]), common::Crc(pServerPlayers->pfDestroyedExplosionTimes[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfShieldDownSoundCooldowns: client={} server={}", i, common::Crc(pClientPlayers->pfShieldDownSoundCooldowns[i]), common::Crc(pServerPlayers->pfShieldDownSoundCooldowns[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pVecAiDirections: client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i,
				XMVectorGetX(pClientPlayers->pVecAiDirections[i]), XMVectorGetY(pClientPlayers->pVecAiDirections[i]), XMVectorGetZ(pClientPlayers->pVecAiDirections[i]),
				XMVectorGetX(pServerPlayers->pVecAiDirections[i]), XMVectorGetY(pServerPlayers->pVecAiDirections[i]), XMVectorGetZ(pServerPlayers->pVecAiDirections[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pVecAiDirections CRC: client={} server={}", i, common::Crc(pClientPlayers->pVecAiDirections[i]), common::Crc(pServerPlayers->pVecAiDirections[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfAiFireTimers: client={} server={}", i, common::Crc(pClientPlayers->pfAiFireTimers[i]), common::Crc(pServerPlayers->pfAiFireTimers[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfAiMissileTimers: client={} server={}", i, common::Crc(pClientPlayers->pfAiMissileTimers[i]), common::Crc(pServerPlayers->pfAiMissileTimers[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] pfAiEdgeCrossCooldowns: client={} server={}", i, common::Crc(pClientPlayers->pfAiEdgeCrossCooldowns[i]), common::Crc(pServerPlayers->pfAiEdgeCrossCooldowns[i]));
			FILE_LOG(0, "[Compare] PR Players[{}] piAiEdgeCrossTargets: client={} server={}", i, common::Crc(pClientPlayers->piAiEdgeCrossTargets[i]), common::Crc(pServerPlayers->piAiEdgeCrossTargets[i]));
		}
	}
	FILE_LOG(0, "[Compare] PR Blasters: client={} server={}", engine::ServerCollectionCrc(*rClientFrame.postRender.pBlasters), engine::ServerCollectionCrc(*rServerFrame.postRender.pBlasters));
	FILE_LOG(0, "[Compare] PR Missiles: client={} server={}", engine::ServerCollectionCrc(*rClientFrame.postRender.pMissiles), engine::ServerCollectionCrc(*rServerFrame.postRender.pMissiles));
	FILE_LOG(0, "[Compare] PR Spaceships: client={} server={}", engine::ServerCollectionCrc(*rClientFrame.postRender.pSpaceships), engine::ServerCollectionCrc(*rServerFrame.postRender.pSpaceships));
	FILE_LOG(0, "[Compare] PR Targets: client={} server={}", engine::ServerCollectionCrc(*rClientFrame.postRender.pTargets), engine::ServerCollectionCrc(*rServerFrame.postRender.pTargets));
}

#endif // BT_CLIENT

} // namespace game
