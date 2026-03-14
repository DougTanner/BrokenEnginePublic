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
	if (mpClientNetwork == nullptr || mSubscriptionQueue.empty())
	{
		return;
	}

	const std::vector<ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (rSlots.at(i).eState == CoordSubscriptionState::kSubscribing ||
		    rSlots.at(i).eState == CoordSubscriptionState::kWaitingFullState ||
		    rSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
		{
			return;
		}
	}

	GridCoord coord = mSubscriptionQueue.front();
	mSubscriptionQueue.erase(mSubscriptionQueue.begin());

	mpClientNetwork->SendSubscribe(coord);
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
	for (const GridCoord& rCoord : rDesiredCoords)
	{
		bool bAlreadySubscribed = false;
		for (int64_t i = 0; i < std::ssize(rSlots); ++i)
		{
			if (rSlots.at(i).coord == rCoord && IsSlotActive(rSlots.at(i)))
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

			if (static_cast<int64_t>(rSub.serverUpdates.size()) >= kiMaxBufferedFrames)
			{
				Log(kLogNetwork, "ClientSessionBase::ApplyReceivedUpdatesBase Buffer full Coord: ({},{}) Size: {} Tick: {}", coord.x, coord.y, rSub.serverUpdates.size(), rUpdate.iTick);
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

	int64_t iRttUs = mpClientNetwork->GetPipelineRttUs();

	int64_t iTickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(tickNs).count();
	int64_t iTargetBehind = (iRttUs > 0) ? ((iRttUs / 2 + iTickTimeUs - 1) / iTickTimeUs + 1) : 1;

	int64_t iOffset = iPreReconcileTick - miLatestServerTick;
	int64_t iError = iOffset + iTargetBehind;
	miClockError = iError;
	miClockOffset = iOffset;
	miClockTargetBehind = iTargetBehind;

	if (std::abs(iError) >= 4)
	{
		Log(kLogNetwork, "ClientSessionBase::ComputeClockCorrectionNs Extreme clock error Error: {} Offset: {} TargetBehind: {} RttUs: {}", iError, iOffset, iTargetBehind, iRttUs);
	}

	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	std::chrono::nanoseconds correction(-iCorrectionSteps * tickNs.count() / 64);

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
		if (rSub.iSnapshotCount >= kiTickRate)
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
	if (rSub.iSnapshotCount >= kiTickRate)
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
		if (rSub.iSnapshotCount >= kiTickRate)
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
