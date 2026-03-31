#include "Pch.h"

#include "Network/Client/ClientSessionBase.h"

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
	mpDiscoveryScanner.reset();
	for (auto& [rCoord, rSub] : game::gpGame->mCoordFrames)
	{
		rSub.ResetClientState();
	}
	mSubscriptionQueue.clear();
	mbServerDiscovered = false;
	miClockError = 0;
	miCurrentTargetBehind = 0;
	mbClockErrorDisconnect = false;
	miConsecutiveClockErrorFrames = 0;
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
		snprintf(mpcDiscoveredAddress, sizeof(mpcDiscoveredAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
		mpDiscoveryScanner.reset();
		mbServerDiscovered = true;
		return true;
	}
	else if (!mpDiscoveryScanner->IsScanning())
	{
		// Timeout — restart scan
		mpDiscoveryScanner.reset();
		StartServerDiscovery();
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
			Log(kLogNetwork, "TrySubscribeNext NoFreeSlot Coord: ({},{})", mSubscriptionQueue.front().x, mSubscriptionQueue.front().y);
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
				Log(kLogNetwork, "UnsubscribeStaleCoords Cancel kSubscribing Slot: {} Coord: ({},{}) CancelledCount: {}", i, unsubCoord.x, unsubCoord.y, mpClientNetwork->GetCancelledSubscriptions().size());
				game::gpGame->mCoordFrames.erase(unsubCoord);
			}
			else
			{
				mpClientNetwork->SendUnsubscribe(i);
				if (rSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
				{
					game::gpGame->mCoordFrames.erase(unsubCoord);
				}
			}
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

	for (int64_t iSlot = 0; iSlot < std::ssize(rCoordSlots); ++iSlot)
	{
		std::vector<ReceivedCoordUpdate>& rSlotUpdates = rAllUpdates.at(iSlot);
		if (rSlotUpdates.empty())
		{
			continue;
		}

		const ClientCoordSlot& rSlot = rCoordSlots.at(iSlot);
		if (rSlot.eState != CoordSubscriptionState::kActive)
		{
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

			int64_t iPrevLatestServerTick = miLatestServerTick;
			miLatestServerTick = std::max(miLatestServerTick, rUpdate.iTick);
			if (miLatestServerTick != iPrevLatestServerTick)
			{
				Log(kLogNetwork, "ClientSessionBase::ApplyReceivedUpdatesBase LatestServerTick advanced Old: {} New: {} Gap: {}", iPrevLatestServerTick, miLatestServerTick, miLatestServerTick - iPrevLatestServerTick);
			}

			if (static_cast<int64_t>(rSub.serverUpdates.size()) >= kiMaxBufferedFrames)
			{
				Log(kLogNetwork, "ClientSessionBase::ApplyReceivedUpdatesBase Buffer full Coord: ({},{}) Size: {} Tick: {}", coord.x, coord.y, rSub.serverUpdates.size(), rUpdate.iTick);
				ASSERT(false);
				bHasNewData = true;
				continue;
			}

			auto [it, bInserted] = rSub.serverUpdates.try_emplace(rUpdate.iTick, CoordFrames::CoordServerUpdate {
				.sharedCrc = rUpdate.sharedCrc,
				.inputCrc = rUpdate.inputCrc,
				.statusChanges = std::move(rUpdate.statusChanges),
			});
			if (bInserted)
			{
				bHasNewData = true;
			}
		}

		rSlotUpdates.clear();
	}

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
		if (miLatestServerTick >= 0)
		{
			Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs No active slots, resetting LatestServerTick from {} to -1", miLatestServerTick);
		}
		miLatestServerTick = -1;
		return 0ns;
	}

	int64_t iRttUs = mpClientNetwork->GetPipelineRttUs();

	int64_t iTickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(tickNs).count();
	// Hysteresis: only update target-behind when the computed value differs by 2+ ticks to avoid oscillation
	int64_t iComputedTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + iTickTimeUs - 1) / iTickTimeUs + 1) : 1;
	if (miCurrentTargetBehind == 0 || std::abs(iComputedTargetBehind - miCurrentTargetBehind) >= 2)
	{
		if (miCurrentTargetBehind != 0 && miCurrentTargetBehind != iComputedTargetBehind)
		{
			Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs TargetBehind changed Old: {} New: {} RttUs: {}", miCurrentTargetBehind, iComputedTargetBehind, iRttUs);
		}
		miCurrentTargetBehind = iComputedTargetBehind;
	}

	int64_t iOffset = iPreReconcileTick - miLatestServerTick;
	int64_t iError = iOffset - miCurrentTargetBehind;
	miClockError = iError;
	miClockOffset = iOffset;
	miClockTargetBehind = miCurrentTargetBehind;

	if (std::abs(iError) >= kiClockErrorDisconnectThreshold)
	{
		++miConsecutiveClockErrorFrames;
		Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs Clock error accumulating ConsecutiveFrames: {} Error: {} Offset: {} TargetBehind: {} RttUs: {} LatestServerTick: {} PreReconcileTick: {}", miConsecutiveClockErrorFrames, iError, iOffset, miCurrentTargetBehind, iRttUs, miLatestServerTick, iPreReconcileTick);
		if (miConsecutiveClockErrorFrames >= kiClockErrorDisconnectConsecutiveFrames)
		{
			mbClockErrorDisconnect = true;
		}
	}
	else
	{
		if (miConsecutiveClockErrorFrames > 0)
		{
			Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs Clock error recovered after {} consecutive frames Error: {} Offset: {} TargetBehind: {}", miConsecutiveClockErrorFrames, iError, iOffset, miCurrentTargetBehind);
		}
		miConsecutiveClockErrorFrames = 0;
	}

	if (std::abs(iError) >= 4)
	{
		Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs Extreme clock error Error: {} Offset: {} TargetBehind: {} RttUs: {} LatestServerTick: {} PreReconcileTick: {}", iError, iOffset, miCurrentTargetBehind, iRttUs, miLatestServerTick, iPreReconcileTick);
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
		if (rpCurrent == nullptr)
		{
			rpNext = nullptr;
			return;
		}
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

int64_t ClientSessionBase::GetClientConfirmedTick() const
{
	auto it = game::gpGame->mCoordFrames.find(game::gpGame->mClientGridCoord);
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
