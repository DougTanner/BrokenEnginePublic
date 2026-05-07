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
	for (auto& [rCoord, rCoordFrames] : game::gpGame->mCoordFrames)
	{
		rCoordFrames.ResetClientState();
	}
	mSubscriptionQueue.clear();
	mbServerDiscovered = false;
	mbDiscoveryScanTimedOut = false;
	miClockError = 0;
	miCurrentTargetBehind = 0;
	miLastLoggedClockTargetBehind = -1;
	miLastPeriodicClockLogTick = -1;
	mbClockErrorDisconnect = false;
	miConsecutiveClockErrorFrames = 0;
	miLastClockErrorLogTick = -1;
	mbNoFreeSlotLogged = false;
}

void ClientSessionBase::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	mpDiscoveryScanner = std::make_unique<NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

void ClientSessionBase::PollLANDiscovery()
{
	if (mpDiscoveryScanner == nullptr)
	{
		return;
	}

	mpDiscoveryScanner->Poll();

	if (mpDiscoveryScanner->IsFound())
	{
		snprintf(mcDiscoveredAddress, sizeof(mcDiscoveredAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
		mpDiscoveryScanner.reset();
		mbServerDiscovered = true;
		return;
	}
	else if (!mpDiscoveryScanner->IsScanning())
	{
		// Timeout — restart scan
		mbDiscoveryScanTimedOut = true;
		mpDiscoveryScanner.reset();
		StartServerDiscovery();
	}
}

// Subscription mechanics

static bool IsSlotActive(const ClientCoordSlot& rSlot)
{
	return rSlot.eState != CoordSubscriptionState::kUnsubscribed &&
	       rSlot.eState != CoordSubscriptionState::kUnsubscribing;
}

void ClientSessionBase::TrySubscribeNext()
{
	if (mpClientNetwork == nullptr || !mpClientNetwork->IsConnected())
	{
		return;
	}

	// Early-out: if no slots are free/freeing, skip entirely — queue is rebuilt each frame anyway
	if (!mSubscriptionQueue.empty())
	{
		bool bAnyFree = false;
		for (const ClientCoordSlot& rSlot : mpClientNetwork->GetCoordSlots())
		{
			if (rSlot.eState == CoordSubscriptionState::kUnsubscribed || rSlot.eState == CoordSubscriptionState::kUnsubscribing)
			{
				bAnyFree = true;
				break;
			}
		}
		if (!bAnyFree)
		{
			if (!mbNoFreeSlotLogged)
			{
				mbNoFreeSlotLogged = true;
				LOG(kNetwork, kWarning, "TrySubscribeNext NoFreeSlot (suppressing until slot frees) Pending: {} First: ({},{})", mSubscriptionQueue.size(), mSubscriptionQueue.front().x, mSubscriptionQueue.front().y);
			}
			return;
		}
		if (mbNoFreeSlotLogged)
		{
			LOG(kNetwork, kDebug, "TrySubscribeNext NoFreeSlot resolved");
		}
		mbNoFreeSlotLogged = false;
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
				LOG(kNetwork, kVerbose, "UnsubscribeStaleCoords Cancel kSubscribing Slot: {} Coord: ({},{}) CancelledCount: {}", i, unsubCoord.x, unsubCoord.y, mpClientNetwork->GetCancelledSubscriptions().size());
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
		CoordFrames& rCoordFrames = game::gpGame->mCoordFrames.at(coord);

		for (ReceivedCoordUpdate& rUpdate : rSlotUpdates)
		{
			if (rUpdate.iTick <= rCoordFrames.iConfirmedTick)
			{
				continue;
			}

			miLatestServerTick = std::max(miLatestServerTick, rUpdate.iTick);

			if (static_cast<int64_t>(rCoordFrames.serverUpdates.size()) >= kiMaxBufferedFrames)
			{
				LOG(kNetwork, kWarning, "ClientSessionBase::ApplyReceivedUpdatesBase Buffer full Coord: ({},{}) Size: {} Tick: {}", coord.x, coord.y, rCoordFrames.serverUpdates.size(), rUpdate.iTick);
				ASSERT(false);
				bHasNewData = true;
				continue;
			}

			auto [it, bInserted] = rCoordFrames.serverUpdates.try_emplace(rUpdate.iTick, CoordFrames::CoordServerUpdate {
				.sharedCrc = rUpdate.sharedCrc,
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
			LOG(kNetwork, kVerbose, "ClientSessionBase::ComputeClockCorrectionNs No active slots, resetting LatestServerTick from {} to -1", miLatestServerTick);
		}
		miLatestServerTick = -1;
		return 0ns;
	}

	int64_t iJitterUs = mpClientNetwork->GetJitterUs();
	int64_t iTickTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(tickNs).count();

	// Jitter buffer: how many ticks behind latestServerTick the sim runs. Pure jitter absorption — RTT is
	// already built into latestServerTick lagging server wall clock by one-way latency, so targetBehind
	// only needs to cover arrival jitter + a fixed safety margin.
	int64_t iTotalBufferUs = iJitterUs + kiJitterSafetyUs;
	int64_t iComputedTargetBehind = (iTotalBufferUs + iTickTimeUs - 1) / iTickTimeUs;

	// Hysteresis: only update when the computed value differs by 2+ ticks to avoid oscillation
	if (miCurrentTargetBehind == 0 || std::abs(iComputedTargetBehind - miCurrentTargetBehind) >= 2)
	{
		if (miCurrentTargetBehind != 0 && miCurrentTargetBehind != iComputedTargetBehind)
		{
			LOG(kNetwork, kVerbose, "ClientSessionBase::ComputeClockCorrectionNs TargetBehind changed Old: {} New: {} JitterUs: {}", miCurrentTargetBehind, iComputedTargetBehind, iJitterUs);
		}
		miCurrentTargetBehind = iComputedTargetBehind;
	}

	// Sim runs BEHIND latestServerTick by miCurrentTargetBehind. Positive iError = sim is past the target
	// (too far ahead — should be prevented by the hard clamp in ClientUpdate). Negative iError = sim is
	// behind the target and needs to catch up via gradual correction or the snap cliff.
	int64_t iTargetSimTick = miLatestServerTick - miCurrentTargetBehind;
	int64_t iOffset = iPreReconcileTick - miLatestServerTick;
	int64_t iError = iPreReconcileTick - iTargetSimTick;
	miClockError = iError;
	miClockOffset = iOffset;
	miClockTargetBehind = miCurrentTargetBehind;

	bool bPeriodicTrigger = (iPreReconcileTick % (kiTickRate * 32) == 0) && (iPreReconcileTick != miLastPeriodicClockLogTick);
	bool bErrorTrigger = std::abs(iError) >= 4 && iPreReconcileTick != miLastClockErrorLogTick;
	if (miCurrentTargetBehind != miLastLoggedClockTargetBehind || bPeriodicTrigger || bErrorTrigger)
	{
		LOG(kNetwork, kVerbose, "ClockSync TargetBehind: {} Error: {} Offset: {} JitterUs: {} LatestServer: {} SimTick: {}", miCurrentTargetBehind, iError, iOffset, iJitterUs, miLatestServerTick, iPreReconcileTick);
		miLastLoggedClockTargetBehind = miCurrentTargetBehind;
		if (bPeriodicTrigger)
		{
			miLastPeriodicClockLogTick = iPreReconcileTick;
		}
		if (bErrorTrigger)
		{
			miLastClockErrorLogTick = iPreReconcileTick;
		}
	}

	if (std::abs(iError) >= kiClockErrorDisconnectThreshold)
	{
		++miConsecutiveClockErrorFrames;
		LOG(kNetwork, kWarning, "ClientSessionBase::ComputeClockCorrectionNs Clock error accumulating ConsecutiveFrames: {} Error: {} Offset: {} TargetBehind: {} JitterUs: {} LatestServerTick: {} PreReconcileTick: {}", miConsecutiveClockErrorFrames, iError, iOffset, miCurrentTargetBehind, iJitterUs, miLatestServerTick, iPreReconcileTick);
		if (miConsecutiveClockErrorFrames >= kiClockErrorDisconnectConsecutiveFrames)
		{
			mbClockErrorDisconnect = true;
		}
	}
	else
	{
		if (miConsecutiveClockErrorFrames > 0)
		{
			LOG(kNetwork, kVerbose, "ClientSessionBase::ComputeClockCorrectionNs Clock error recovered after {} consecutive frames Error: {} Offset: {} TargetBehind: {}", miConsecutiveClockErrorFrames, iError, iOffset, miCurrentTargetBehind);
		}
		miConsecutiveClockErrorFrames = 0;
	}

	if (std::abs(iError) < 4)
	{
		miLastClockErrorLogTick = -1;
	}

	int64_t iCorrectionSteps = std::clamp(iError, -4LL, 4LL);
	int64_t iDivisor = (std::abs(iError) >= 4) ? 8 : 64;
	std::chrono::nanoseconds correction(-iCorrectionSteps * tickNs.count() / iDivisor);

	return correction;
}

// Queries

int64_t ClientSessionBase::GetConfirmedTick() const
{
	int64_t iMin = -1;
	for (const auto& [rCoord, rCoordFrames] : game::gpGame->mCoordFrames)
	{
		if (rCoordFrames.iConfirmedTick >= 0 && (iMin < 0 || rCoordFrames.iConfirmedTick < iMin))
		{
			iMin = rCoordFrames.iConfirmedTick;
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
	for (const auto& [rCoord, rCoordFrames] : game::gpGame->mCoordFrames)
	{
		if (rCoordFrames.iConfirmedTick >= 0)
		{
			iTotal += static_cast<int64_t>(rCoordFrames.serverUpdates.size());
		}
	}
	return iTotal;
}

} // namespace engine

#endif // BT_CLIENT
