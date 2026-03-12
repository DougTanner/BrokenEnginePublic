#include "Game.h"

#include "Network/ClientSession.h"
#include "Frame/Collections/Players/Players.h"
#include "Frame/Collections/Blasters/Blasters.h"
#include "Frame/Collections/Missiles/Missiles.h"
#include "Frame/Collections/Spaceships/Spaceships.h"
#include "Frame/Collections/Targets/Targets.h"
#include "Profile/ProfileManager.h"

namespace game
{

#if defined(BT_CLIENT)

ClientSession::ClientSession()
{
	gpClientSession = this;

	if constexpr (kbEnableReconcileThread)
	{
		mpReconcileWorker = std::make_unique<common::PersistentWorker>(common::kThreadReconcile, 10 * 1'024 * 1'024);
	}
}

ClientSession::~ClientSession()
{
	if constexpr (kbEnableReconcileThread)
	{
		if (mbReconcileInFlight)
		{
			mpReconcileWorker->Wait();
			mbReconcileInFlight = false;
		}
	}

	gpClientSession = nullptr;
}

bool ClientSession::IsExtrapolating() const
{
	if (!IsNetworkMode())
	{
		return false;
	}
	for (const auto& [rCoord, rFrame] : gpGame->mCoordFrames)
	{
		if (rFrame.iConfirmedTick >= 0)
		{
			return true;
		}
	}
	return false;
}

void ClientSession::PrepareExtrapolationTick(const std::vector<engine::GridCoord>& rActiveCoords)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = gpGame->mCoordFrames.find(rCoord);
		if (subIt == gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
		{
			continue;
		}
		engine::CoordFrames& rSub = subIt->second;
		if (rSub.iSnapshotCount >= engine::kiTickRate)
		{
			// Ring full: try to reclaim oldest pre-confirmed slot
			if (rSub.iConfirmedOffset > 0)
			{
				rSub.iSnapshotHead = SnapshotIndex(rSub.iSnapshotHead, 1);
				--rSub.iSnapshotCount;
				--rSub.iConfirmedOffset;
			}
			else
			{
				continue;
			}
		}
		int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
		if (rSub.snapshots[iPhysical] == nullptr)
		{
			rSub.snapshots[iPhysical] = std::make_unique<Frame>();
		}
	}
}

void ClientSession::BuildExtrapolationFrameRef(const engine::GridCoord& rCoord, Frame*& rpNext, Frame*& rpCurrent)
{
	auto subIt = gpGame->mCoordFrames.find(rCoord);
	if (subIt == gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
	{
		return;
	}
	engine::CoordFrames& rSub = subIt->second;
	if (rSub.iSnapshotCount >= engine::kiTickRate)
	{
		return;
	}
	int64_t iNextPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
	rpNext = rSub.snapshots[iNextPhysical].get();
	if (rSub.iSnapshotCount == 0)
	{
		rpCurrent = &gpGame->CurrentFrame(rCoord);
	}
	else
	{
		int64_t iCurrentPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount - 1);
		rpCurrent = rSub.snapshots[iCurrentPhysical].get();
	}
}

void ClientSession::RecordExtrapolationSnapshot(const std::vector<engine::GridCoord>& rActiveCoords, [[maybe_unused]] int64_t iTick)
{
	for (const engine::GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = gpGame->mCoordFrames.find(rCoord);
		if (subIt == gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
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

Frame* ClientSession::GetSnapshotFrame(engine::GridCoord coord) const
{
	auto subIt = gpGame->mCoordFrames.find(coord);
	if (subIt == gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0 || subIt->second.iSnapshotCount <= 0)
	{
		return nullptr;
	}
	const engine::CoordFrames& rSub = subIt->second;
	int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount - 1);
	return rSub.snapshots[iPhysical].get();
}

void ClientSession::ConnectToServer(const char* pServerAddress)
{
	FILE_LOG(0, "ConnectToServer: connecting to {}", pServerAddress);
	gpGame->mModalMessage[0] = '\0';
	// Heap: ClientNetwork allocates ENet host and peer
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpClientNetwork = std::make_unique<engine::ClientNetwork>(pServerAddress, engine::kuiDefaultPort, kiDesiredCoordSlots);
}

void ClientSession::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<engine::NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

std::chrono::nanoseconds ClientSession::ComputeClockCorrectionNs(int64_t iPreReconcileTick)
{
	if (miLatestServerTick < 0)
	{
		return 0ns;
	}

	int64_t iRttUs = mpClientNetwork->GetPipelineRttUs();

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

	common::Log("GameClient: ClockCorrection offset={} target={} error={} correction={}", iOffset, iTargetBehind, iError, iCorrectionSteps); // DT: TEMP
	return correction;
}

void ClientSession::DisconnectFromServer()
{
	common::Log("GameClient: DisconnectFromServer"); // DT: TEMP
	// Heap: ClientNetwork destructor triggers ENet disconnect and cleanup
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
	mpClientNetwork.reset();
	// Reset client fields on all subscribed frames
	for (auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		rSub.iConfirmedTick = -1;
		rSub.iConfirmedOffset = -1;
		rSub.iSnapshotHead = 0;
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

void ClientSession::PollNetwork()
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

	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Heap: ENet polling allocates packets, DrainReceived* moves vectors, stringstream serialization
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mpClientNetwork->Poll();

	// Wait for server connection response before entering game
	if (!mpClientNetwork->IsConnectionAccepted())
	{
		const char* pRejection = mpClientNetwork->GetRejectionReason();
		if (pRejection != nullptr)
		{
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "%s", pRejection);
			DisconnectFromServer();
			gpGame->meUiState = UiState::kModal;
			return;
		}

		if (mpClientNetwork->WasDisconnected())
		{
			snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Connection failed");
			DisconnectFromServer();
			gpGame->meUiState = UiState::kModal;
			return;
		}

		return;
	}

	// Connection accepted - transition to game mode (runs once)
	if (gpGame->InMainMenu())
	{
		gpGame->StartGameMusic();
		gpGame->CreateNewFrame(GameFlags::kGame);
		gpGame->mGameFlags.Clear(engine::GameFlags::kMainMenu);
		gpGame->Reset();
		gpGame->meUiState = UiState::kNone;
		common::Log("GameClient: Connection accepted, entering game"); // DT: TEMP
	}

	// Check for debug frame response
	std::unique_ptr<engine::ReceivedDebugFrame> pDebugFrame = mpClientNetwork->DrainReceivedDebugFrame();
	if (pDebugFrame != nullptr && mDesyncDebugState.pClientFrame != nullptr)
	{
		FILE_LOG(0, "[PollNetwork] Debug frame received for frame={} coord=({},{}), running CompareWithServerFrame", mDesyncDebugState.iTick, mDesyncDebugState.coord.x, mDesyncDebugState.coord.y);
		CompareWithServerFrame(*mDesyncDebugState.pClientFrame, *pDebugFrame->pFrame, mDesyncDebugState.iTick, mDesyncDebugState.coord);
		mDesyncDebugState = {};
		DEBUG_BREAK();
		snprintf(gpGame->mModalMessage, sizeof(gpGame->mModalMessage), "Desynced from server");
		mpClientNetwork->Disconnect();
		return;
	}

	if (mpClientNetwork->WasDisconnected())
	{
		FILE_LOG(0, "[PollNetwork] Disconnected while waiting for debug frame: desyncFrame={}", mDesyncDebugState.iTick);
		gpGame->ChangeFrame(GameFlags::kMainMenu);
		gpGame->meUiState = gpGame->mModalMessage[0] != '\0' ? UiState::kModal : UiState::kPause;
		return;
	}

	// Waiting for debug frame response — skip normal processing
	if (mDesyncDebugState.iTick >= 0)
	{
		return;
	}

	// Check for player assignments
	for (const engine::ReceivedAssignment& rAssignment : mpClientNetwork->DrainReceivedAssignments())
	{
		if (rAssignment.playerId != gpGame->HumanPlayerId())
		{
			player_t oldHumanPlayerId = gpGame->HumanPlayerId();
			gpGame->SetHumanPlayerId(rAssignment.playerId);
			gpGame->mHumanGridCoord = rAssignment.coord;

			FILE_LOG(0, "[PollNetwork] Assignment: old={} new={} grid=({},{}) frame={}", oldHumanPlayerId.ToUuid().Value(), gpGame->HumanPlayerId().ToUuid().Value(), gpGame->mHumanGridCoord.x, gpGame->mHumanGridCoord.y, gpGame->TickCounter());

			// Trigger subscription updates for the new grid position
			UpdateSubscriptions();
		}
	}

	// Process server-authoritative player state notifications
	for (const engine::ReceivedPlayerState& rState : mpClientNetwork->DrainReceivedPlayerStates())
	{
		switch (rState.eType)
		{
		case engine::PlayerStateType::kSpawned:
		case engine::PlayerStateType::kChangedFrame:
			gpGame->mHumanGridCoord = rState.coord;
			common::Log("GameClient: PlayerState type={} coord ({},{})", static_cast<int>(rState.eType), rState.coord.x, rState.coord.y); // DT: TEMP
			break;
		case engine::PlayerStateType::kDied:
			if (gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord))
			{
				gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.gameFlags.Set(GameFlags::kDeathScreen);
			}
			gpGame->SetHumanPlayerId({});
			gpGame->SetPreviousHumanArmor(0.0f);
			common::Log("GameClient: Player died"); // DT: TEMP
			break;
		}
	}

	ApplyReceivedFullStates();
	UpdateSubscriptions();
	ApplyReceivedUpdates();
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

		engine::CoordFrames& rSub = gpGame->mCoordFrames[coord];
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

			mConfirmedHumanState.humanGridCoord = gpGame->mHumanGridCoord;
			mConfirmedHumanState.humanPlayerId = gpGame->HumanPlayerId();
			mConfirmedHumanState.fPreviousHumanArmor = gpGame->PreviousHumanArmor();
			// Only set confirmed time from the first coord's full state;
			// later coords arrive at higher frame numbers and would desync the
			// confirmed time vs. iMinConfirmedFrame during reconciliation rollback
			if (mConfirmedHumanState.fCurrentTime == 0.0f)
			{
				mConfirmedHumanState.fCurrentTime = rSub.pCurrent->interpolate.fCurrentTime;
			}

			mbReconcileHasNewData = true;
			FILE_LOG(0, "[ApplyReceivedFullStates] Initial coord=({},{}) tick={}", coord.x, coord.y, iTick);
			common::Log("GameClient: FullState initial coord ({},{}) tick={} tickCounter={}", coord.x, coord.y, iTick, gpGame->TickCounter()); // DT: TEMP
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
			common::Log("GameClient: FullState deferred coord ({},{}) tick={} confirmedTick={}", coord.x, coord.y, iTick, rSub.iConfirmedTick); // DT: TEMP
		}
	}

	// Try subscribing to the next coord in the queue
	TrySubscribeNext();
}

void ClientSession::ApplyReceivedUpdates()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	const std::vector<engine::ClientCoordSlot>& rCoordSlots = mpClientNetwork->GetCoordSlots();
	std::vector<std::vector<engine::ReceivedCoordUpdate>>& rAllUpdates = mpClientNetwork->DrainReceivedCoordUpdates();

	for (int64_t iSlot = 0; iSlot < std::ssize(rCoordSlots); ++iSlot)
	{
		std::vector<engine::ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates.at(iSlot);
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const engine::ClientCoordSlot& rSlot = rCoordSlots.at(iSlot);
		if (rSlot.eState != engine::CoordSubscriptionState::kActive)
		{
			rSlotUpdates.clear();
			continue;
		}

		engine::GridCoord coord = rSlot.coord;
		engine::CoordFrames& rSub = gpGame->mCoordFrames[coord];

		for (engine::ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			// Skip frames at or before confirmed frame for this coord
			if (rUpdate.iTick <= rSub.iConfirmedTick)
			{
				common::Log("GameClient: ServerUpdate skip confirmed coord ({},{}) tick={} confirmedTick={}", coord.x, coord.y, rUpdate.iTick, rSub.iConfirmedTick); // DT: TEMP
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
				common::Log("GameClient: ServerUpdate coord ({},{}) tick={} serverCrc={} inputCrc={} changes={}", coord.x, coord.y, rUpdate.iTick, rUpdate.serverCrc, rUpdate.inputCrc, rSub.serverUpdates[rUpdate.iTick].statusChanges.size()); // DT: TEMP
			}
		}

		rSlotUpdates.clear();
	}
	common::Log("GameClient: ApplyReceivedUpdates latestServerTick={}", miLatestServerTick); // DT: TEMP
}

int64_t ClientSession::GetConfirmedTick() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0 && (iMin < 0 || rSub.iConfirmedTick < iMin))
		{
			iMin = rSub.iConfirmedTick;
		}
	}
	return iMin;
}

int64_t ClientSession::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rSub] : gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0)
		{
			iTotal += static_cast<int64_t>(rSub.serverUpdates.size());
		}
	}
	return iTotal;
}

void ClientSession::UpdateSubscriptions()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	// Player alive but not yet in assigned cell (mid-transfer): maintain current subscriptions
	// Only guard when coord has confirmed server data — initial spawn must subscribe first
	if (gpGame->HumanPlayerId().IsValid() && gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord)
		&& gpGame->mCoordFrames[gpGame->mHumanGridCoord].iConfirmedTick >= 0
		&& !gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.pPlayers->idToIndexMap.contains(gpGame->HumanPlayerId()))
	{
		return;
	}

	// Heap: vector operations for subscription queue
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	// Compute desired coords (priority-ordered)
	std::vector<engine::GridCoord> desiredCoords;
	desiredCoords.reserve(5);

	bool bDead = !gpGame->HumanPlayerId().IsValid()
		&& gpGame->mCoordFrames.contains(gpGame->mHumanGridCoord)
		&& (gpGame->CurrentFrame(gpGame->mHumanGridCoord).interpolate.gameFlags & GameFlags::kDeathScreen);

	if (gpGame->HumanPlayerId().IsValid())
	{
		desiredCoords.push_back(gpGame->mHumanGridCoord);

#if 0 // DT: TEMP
		XMVECTOR vecArea = gpGame->CurrentFrame(gpGame->mHumanGridCoord).postRender.vecArea;
		XMVECTOR vecPos = gpGame->GetHumanPlayerPosition();
		float fCenterX = (XMVectorGetX(vecArea) + XMVectorGetZ(vecArea)) * 0.5f;
		float fCenterY = (XMVectorGetY(vecArea) + XMVectorGetW(vecArea)) * 0.5f;

		engine::GridCoord quadrantOffsets[3];
		engine::ComputeQuadrantOffsets(XMVectorGetX(vecPos), XMVectorGetY(vecPos), fCenterX, fCenterY, quadrantOffsets);

		for (const engine::GridCoord& rOffset : quadrantOffsets)
		{
			engine::GridCoord neighbor {gpGame->mHumanGridCoord.x + rOffset.x, gpGame->mHumanGridCoord.y + rOffset.y};
			desiredCoords.push_back(neighbor);
		}
#endif
	}
	else if (bDead)
	{
		// Dead: keep death coord + pre-emptive origin for respawn
		desiredCoords.push_back(gpGame->mHumanGridCoord);
		if (gpGame->mHumanGridCoord != engine::kOriginCoord)
		{
			desiredCoords.push_back(engine::kOriginCoord);
		}
	}
	else
	{
		// Not yet assigned: just origin
		desiredCoords.push_back(engine::kOriginCoord);
	}

	std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();

	// Unsubscribe from coords no longer desired
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (rSlots.at(i).eState == engine::CoordSubscriptionState::kUnsubscribed ||
		    rSlots.at(i).eState == engine::CoordSubscriptionState::kUnsubscribing)
		{
			continue;
		}

		if (!std::ranges::contains(desiredCoords, rSlots.at(i).coord))
		{
			engine::GridCoord unsubCoord = rSlots.at(i).coord;
			if (rSlots.at(i).eState == engine::CoordSubscriptionState::kSubscribing)
			{
				rSlots.at(i) = {};
			}
			else
			{
				mpClientNetwork->SendUnsubscribe(i);
			}
			// Reset client fields on unsubscribed coord
			auto unsubIt = gpGame->mCoordFrames.find(unsubCoord);
			if (unsubIt != gpGame->mCoordFrames.end())
			{
				engine::CoordFrames& rUnSub = unsubIt->second;
				rUnSub.iConfirmedTick = -1;
				rUnSub.iConfirmedOffset = -1;
				rUnSub.iSnapshotHead = 0;
				rUnSub.iSnapshotCount = 0;
				rUnSub.serverUpdates.clear();
				rUnSub.pendingFullState.reset();
				rUnSub.uiGeneration = 0;
			}
			FILE_LOG(0, "[UpdateSubscriptions] Unsubscribe slot={} coord=({},{})", i, unsubCoord.x, unsubCoord.y);
			common::Log("GameClient: Unsubscribe slot {} coord ({},{})", i, unsubCoord.x, unsubCoord.y); // DT: TEMP
		}
	}

	// Build subscription queue: desired coords not yet subscribed (in priority order)
	mSubscriptionQueue.clear();
	for (const engine::GridCoord& rCoord : desiredCoords)
	{
		bool bAlreadySubscribed = false;
		for (int64_t i = 0; i < std::ssize(rSlots); ++i)
		{
			if (rSlots.at(i).coord == rCoord &&
			    rSlots.at(i).eState != engine::CoordSubscriptionState::kUnsubscribed &&
			    rSlots.at(i).eState != engine::CoordSubscriptionState::kUnsubscribing)
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

	common::Log("GameClient: UpdateSubscriptions desired={} queue={}", desiredCoords.size(), mSubscriptionQueue.size()); // DT: TEMP

	// Start subscribing
	TrySubscribeNext();
}

void ClientSession::TrySubscribeNext()
{
	if (mpClientNetwork == nullptr || mSubscriptionQueue.empty())
	{
		return;
	}

	// Check if any slot is currently subscribing or waiting for full state
	const std::vector<engine::ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (rSlots.at(i).eState == engine::CoordSubscriptionState::kSubscribing ||
		    rSlots.at(i).eState == engine::CoordSubscriptionState::kWaitingFullState ||
		    rSlots.at(i).eState == engine::CoordSubscriptionState::kUnsubscribing)
		{
			return; // Wait for current subscription or unsubscription to complete
		}
	}

	engine::GridCoord coord = mSubscriptionQueue.front();
	mSubscriptionQueue.erase(mSubscriptionQueue.begin());

	mpClientNetwork->SendSubscribe(coord);
	FILE_LOG(0, "[TrySubscribeNext] Subscribe coord=({},{}) remaining={}", coord.x, coord.y, mSubscriptionQueue.size());
}

void ClientSession::WaitForReconcile()
{
	if (mpClientNetwork == nullptr)
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

		common::Log("GameClient: WaitForReconcile waiting..."); // DT: TEMP
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
		common::Log("GameClient: WaitForReconcile complete"); // DT: TEMP
		mbReconcileInFlight = false;
	}
}

void ClientSession::TryKickReconcile()
{
	if (mpClientNetwork == nullptr || GetConfirmedTick() < 0)
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

	common::Log("GameClient: TryKickReconcile confirmedTick={} tickCounter={} latestServer={}", GetConfirmedTick(), gpGame->TickCounter(), miLatestServerTick); // DT: TEMP
	KickReconcile();
	mbReconcileInFlight = true;
	mbReconcileHasNewData = false;
}

void ClientSession::CompareWithServerFrame(const Frame& rClientFrame, const Frame& rServerFrame, [[maybe_unused]] int64_t iTick, [[maybe_unused]] engine::GridCoord coord)
{
	FILE_LOG(0, "[CompareWithServerFrame] tick={} coord=({},{})", iTick, coord.x, coord.y);
	FILE_LOG(0, "[CompareWithServerFrame] client pushers: count={} mapSize={}", rClientFrame.interpolate.pushers.iCount, rClientFrame.interpolate.pushers.idToIndexMap.size());
	FILE_LOG(0, "[CompareWithServerFrame] server pushers: count={} mapSize={}", rServerFrame.interpolate.pushers.iCount, rServerFrame.interpolate.pushers.idToIndexMap.size());

	// Suppress DEBUG_BREAK so the full comparison chain runs
	common::gbSuppressVerifyFrameBreak = true;
	rClientFrame.ServerCompare(rServerFrame);
	common::gbSuppressVerifyFrameBreak = false;
}

void ClientSession::PollAndReconcile()
{
	// Desync debug mode: only poll network for debug frame response, keep window responsive
	if (GetDesyncTick() >= 0)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		PollNetwork();
		if (engine::gpClientNetwork != nullptr)
		{
			engine::gpClientNetwork->Flush();
		}
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
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkPollReconcile, true);
}

void ClientSession::PostTick()
{
	if (GetDesyncTick() >= 0) return;

	// Post-tick: poll network before render so reconciliation gets the freshest data
	{
		// Heap: ENet polling
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		PollNetwork();
	}

	gpProfileManager->CpuStart(engine::kCpuTimerNetworkSend);
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		if (engine::gpClientNetwork != nullptr)
		{
			engine::gpClientNetwork->SendAck();
			engine::gpClientNetwork->Flush();
		}
	}
	gpProfileManager->CpuStop(engine::kCpuTimerNetworkSend, true);
}

void ClientSession::PostRender()
{
	if (GetDesyncTick() >= 0) return;

	// Kick reconcile after render so snapshots remain valid for GetSnapshotFrame during rendering
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	TryKickReconcile();
}

#endif // BT_CLIENT

} // namespace game
