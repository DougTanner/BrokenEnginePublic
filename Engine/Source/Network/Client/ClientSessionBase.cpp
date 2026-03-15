#include "Pch.h"
#include "Game.h"

#if defined(BT_CLIENT)

namespace engine
{

// Connection lifecycle

void ClientSessionBase::ConnectToServer(std::string_view serverAddress, uint16_t uiPort, int64_t iCoordSlots)
{
	// Heap: ClientNetwork allocates ENet host and peer
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	miCoordSlots = iCoordSlots;
	mpClientNetwork = std::make_unique<Client>(serverAddress.data(), uiPort, iCoordSlots);
}

void ClientSessionBase::DisconnectFromServerBase()
{
	// Heap: ClientNetwork destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	miLatestServerTick = -1;
	mpClientNetwork.reset();
	for (auto& [rCoord, rSub] : game::gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}
	mSubscriptionQueue.clear();
	miClockError = 0;
	miCurrentTargetBehind = 0;
	mbClockErrorDisconnect = false;
}

void ClientSessionBase::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

bool ClientSessionBase::PollLANDiscovery()
{
	if (mpDiscoveryScanner == nullptr)
	{
		return false;
	}

	mpDiscoveryScanner->Poll();

	if (mpDiscoveryScanner->IsFound())
	{
		char pcAddress[16] {};
		snprintf(pcAddress, sizeof(pcAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
		mpDiscoveryScanner.reset();
		ConnectToServer(pcAddress, kuiDefaultPort, miCoordSlots);
		return true;
	}
	else if (!mpDiscoveryScanner->IsScanning())
	{
		DEBUG_BREAK();
		mpDiscoveryScanner.reset();
	}
	return false;
}

// Subscription mechanics

static bool IsSlotActive(const ClientCoordSlot& rSlot)
{
	return rSlot.eState != CoordSubscriptionState::kUnsubscribed &&
	       rSlot.eState != CoordSubscriptionState::kUnsubscribing;
}

void ClientSessionBase::TrySubscribeNext()
{
	if (mpClientNetwork == nullptr)
	{
		return;
	}

	while (!mSubscriptionQueue.empty())
	{
		if (!mpClientNetwork->SendSubscribe(mSubscriptionQueue.front()))
		{
			break;
		}
		mSubscriptionQueue.erase(mSubscriptionQueue.begin());
	}
}

void ClientSessionBase::UnsubscribeStaleCoords(const std::vector<GridCoord>& rDesiredCoords)
{
	std::vector<ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();

	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (!IsSlotActive(rSlots.at(i)))
		{
			continue;
		}

		if (!std::ranges::contains(rDesiredCoords, rSlots.at(i).coord))
		{
			GridCoord unsubCoord = rSlots.at(i).coord;
			if (rSlots.at(i).eState == CoordSubscriptionState::kSubscribing)
			{
				rSlots.at(i) = {};
				mpClientNetwork->GetCancelledSubscriptions().push_back(unsubCoord);
			}
			else
			{
				mpClientNetwork->SendUnsubscribe(i);
			}
			game::gpGame->mCoordFrames.erase(unsubCoord);
		}
	}
}

void ClientSessionBase::BuildSubscriptionQueue(const std::vector<GridCoord>& rDesiredCoords)
{
	const std::vector<ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();

	mSubscriptionQueue.clear();
	GridCoord activeCoords[16];
	int64_t iActiveCount = 0;
	for (const ClientCoordSlot& rSlot : rSlots)
	{
		if (IsSlotActive(rSlot))
		{
			activeCoords[iActiveCount++] = rSlot.coord;
		}
	}
	for (const GridCoord& rCoord : rDesiredCoords)
	{
		bool bAlreadyActive = false;
		for (int64_t i = 0; i < iActiveCount; ++i)
		{
			if (activeCoords[i] == rCoord)
			{
				bAlreadyActive = true;
				break;
			}
		}
		if (!bAlreadyActive)
		{
			mSubscriptionQueue.push_back(rCoord);
		}
	}
}

// Update buffering

bool ClientSessionBase::ApplyReceivedUpdatesBase()
{
	// Heap: map insertion for per-frame server updates
	ScopedSuppressAllocationTracking suppressAllocationTracking;

	bool bHasNewData = false;
	const std::vector<ClientCoordSlot>& rCoordSlots = mpClientNetwork->GetCoordSlots();
	std::vector<std::vector<ReceivedCoordUpdate>>& rAllUpdates = mpClientNetwork->DrainReceivedCoordUpdates();

	int64_t iTotalDrained = 0; // DT: TEMP
	int64_t iTotalApplied = 0; // DT: TEMP

	for (int64_t iSlot = 0; iSlot < std::ssize(rCoordSlots); ++iSlot)
	{
		std::vector<ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates.at(iSlot);
		iTotalDrained += std::ssize(rSlotUpdates); // DT: TEMP
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const ClientCoordSlot& rSlot = rCoordSlots.at(iSlot);
		if (rSlot.eState != CoordSubscriptionState::kActive)
		{
			Log(kLogNetwork, "ApplyReceivedUpdatesBase skipping slot {} state: {} updates: {}", iSlot, static_cast<int>(rSlot.eState), rSlotUpdates.size()); // DT: TEMP
			rSlotUpdates.clear();
			continue;
		}

		GridCoord coord = rSlot.coord;
		CoordFrames& rSub = game::gpGame->mCoordFrames.at(coord);

		for (ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			if (rUpdate.iTick <= rSub.iConfirmedTick)
			{
				continue;
			}

			miLatestServerTick = std::max(miLatestServerTick, rUpdate.iTick);

			if (static_cast<int64_t>(rSub.serverUpdates.size()) >= kiMaxBufferedFrames)
			{
				Log(kLogNetwork, "ClientSessionBase::ApplyReceivedUpdatesBase Buffer full Coord: ({},{}) Size: {} Tick: {}", coord.x, coord.y, rSub.serverUpdates.size(), rUpdate.iTick);
				ASSERT(false);
				bHasNewData = true;
				continue;
			}

			auto [it, bInserted] = rSub.serverUpdates.try_emplace(rUpdate.iTick, CoordFrames::CoordServerUpdate {
				.serverCrc = rUpdate.serverCrc,
				.inputCrc = rUpdate.inputCrc,
				.statusChanges = std::move(rUpdate.statusChanges),
			});
			if (bInserted)
			{
				bHasNewData = true;
				++iTotalApplied; // DT: TEMP
			}
		}

		rSlotUpdates.clear();
	}

	Log(kLogNetwork, "ApplyReceivedUpdatesBase drained: {} applied: {} latestServerTick: {}", iTotalDrained, iTotalApplied, miLatestServerTick); // DT: TEMP

	return bHasNewData;
}

// Clock correction

std::chrono::nanoseconds ClientSessionBase::ComputeClockCorrectionNs(int64_t iPreReconcileTick, std::chrono::nanoseconds tickNs)
{
	if (miLatestServerTick < 0)
	{
		return 0ns;
	}

	// No active subscriptions means no data can arrive - reset clock state
	bool bHasActiveSlot = false;
	for (const ClientCoordSlot& rSlot : mpClientNetwork->GetCoordSlots())
	{
		if (rSlot.eState == CoordSubscriptionState::kActive)
		{
			bHasActiveSlot = true;
			break;
		}
	}
	if (!bHasActiveSlot)
	{
		miLatestServerTick = -1;
		return 0ns;
	}

	int64_t iRttUs = mpClientNetwork->GetPipelineRttUs();

	int64_t iTickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(tickNs).count();
	// Hysteresis: only update target-behind when the computed value differs by 2+ ticks to avoid oscillation
	int64_t iComputedTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + iTickTimeUs - 1) / iTickTimeUs + 1) : 1;
	if (miCurrentTargetBehind == 0 || std::abs(iComputedTargetBehind - miCurrentTargetBehind) >= 2)
	{
		miCurrentTargetBehind = iComputedTargetBehind;
	}

	int64_t iOffset = iPreReconcileTick - miLatestServerTick;
	int64_t iError = iOffset - miCurrentTargetBehind;
	miClockError = iError;
	miClockOffset = iOffset;
	miClockTargetBehind = miCurrentTargetBehind;

	if (std::abs(iError) >= kiClockErrorDisconnectThreshold)
	{
		mbClockErrorDisconnect = true;
	}

	if (std::abs(iError) >= 4)
	{
		Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs Extreme clock error Error: {} Offset: {} TargetBehind: {} RttUs: {} LatestServerTick: {} PreReconcileTick: {}", iError, iOffset, miCurrentTargetBehind, iRttUs, miLatestServerTick, iPreReconcileTick); // DT: TEMP
	}

	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	int64_t iDivisor = (std::abs(iError) >= 4) ? 16 : 64;
	std::chrono::nanoseconds correction(-iCorrectionSteps * tickNs.count() / iDivisor);

	return correction;
}

// Extrapolation

bool ClientSessionBase::IsExtrapolating() const
{
	if (mpClientNetwork == nullptr)
	{
		return false;
	}
	for (const auto& [rCoord, rFrame] : game::gpGame->mCoordFrames)
	{
		if (rFrame.iConfirmedTick >= 0)
		{
			return true;
		}
	}
	return false;
}

void ClientSessionBase::PrepareExtrapolationTick(const std::vector<GridCoord>& rActiveCoords)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = game::gpGame->mCoordFrames.find(rCoord);
		if (subIt == game::gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
		{
			continue;
		}
		CoordFrames& rSub = subIt->second;
		if (rSub.iSnapshotCount >= kiNetworkBufferSize)
		{
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
			rSub.snapshots[iPhysical] = std::make_unique<game::Frame>();
		}
	}
}

void ClientSessionBase::BuildExtrapolationFrameRef(const GridCoord& rCoord, game::Frame*& rpNext, game::Frame*& rpCurrent)
{
	auto subIt = game::gpGame->mCoordFrames.find(rCoord);
	if (subIt == game::gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
	{
		return;
	}
	CoordFrames& rSub = subIt->second;
	if (rSub.iSnapshotCount >= kiNetworkBufferSize)
	{
		return;
	}
	int64_t iNextPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount);
	rpNext = rSub.snapshots[iNextPhysical].get();
	if (rSub.iSnapshotCount == 0)
	{
		rpCurrent = &game::gpGame->CurrentFrame(rCoord);
	}
	else
	{
		int64_t iCurrentPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount - 1);
		rpCurrent = rSub.snapshots[iCurrentPhysical].get();
	}
}

void ClientSessionBase::RecordExtrapolationSnapshot(const std::vector<GridCoord>& rActiveCoords, [[maybe_unused]] int64_t iTick)
{
	for (const GridCoord& rCoord : rActiveCoords)
	{
		auto subIt = game::gpGame->mCoordFrames.find(rCoord);
		if (subIt == game::gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0)
		{
			continue;
		}
		CoordFrames& rSub = subIt->second;
		if (rSub.iSnapshotCount >= kiNetworkBufferSize)
		{
			continue;
		}
		rSub.iSnapshotCount++;
	}
}

game::Frame* ClientSessionBase::GetSnapshotFrame(GridCoord coord) const
{
	auto subIt = game::gpGame->mCoordFrames.find(coord);
	if (subIt == game::gpGame->mCoordFrames.end() || subIt->second.iConfirmedTick < 0 || subIt->second.iSnapshotCount <= 0)
	{
		return nullptr;
	}
	const CoordFrames& rSub = subIt->second;
	int64_t iPhysical = SnapshotIndex(rSub.iSnapshotHead, rSub.iSnapshotCount - 1);
	if (rSub.snapshots[iPhysical] == nullptr)
	{
		return nullptr;
	}
	Log(kLogNetwork, "renderTick: {} snapshotCount: {} confirmed: {}", rSub.snapshots[iPhysical]->interpolate.iTick, rSub.iSnapshotCount, rSub.iConfirmedTick); // DT: TEMP
	return rSub.snapshots[iPhysical].get();
}

// Queries

int64_t ClientSessionBase::GetConfirmedTick() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rSub] : game::gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0 && (iMin < 0 || rSub.iConfirmedTick < iMin))
		{
			iMin = rSub.iConfirmedTick;
		}
	}
	return iMin;
}

int64_t ClientSessionBase::GetHumanConfirmedTick() const
{
	auto it = game::gpGame->mCoordFrames.find(game::gpGame->mHumanGridCoord);
	if (it == game::gpGame->mCoordFrames.end() || it->second.iConfirmedTick < 0)
	{
		return -1;
	}
	return it->second.iConfirmedTick;
}

int64_t ClientSessionBase::GetServerUpdateBufferSize() const
{
	int64_t iTotal = 0;
	for (const auto& [rCoord, rSub] : game::gpGame->mCoordFrames)
	{
		if (rSub.iConfirmedTick >= 0)
		{
			iTotal += static_cast<int64_t>(rSub.serverUpdates.size());
		}
	}
	return iTotal;
}

} // namespace engine

#endif // BT_CLIENT
