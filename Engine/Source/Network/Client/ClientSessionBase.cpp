#include "Pch.h"

#include "Network/Client/ClientSessionBase.h"

#if defined(BT_CLIENT)

#include "Game.h"

namespace engine
{

// Connection lifecycle

namespace
{

ClientGuid LoadClientGuidFromDisk()
{
	// Heap: ReadVersionedFile opens an fstream and a filesystem path for GUID file I/O
	ScopedSuppressAllocationTracking suppress;

	ClientGuid loadedGuid {};
	if (ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("ClientGuid.bin"), loadedGuid) && !loadedGuid.IsEmpty())
	{
		LOG(kNetwork, kInfo, "ClientSessionBase loaded GUID from disk: {} {}", loadedGuid.uiHigh, loadedGuid.uiLow);
		return loadedGuid;
	}
	return {};
}

void PersistClientGuidToDisk(const ClientGuid& rGuid)
{
	// WriteVersionedFile persists atomically (a mid-write crash would otherwise empty the file and orphan all server-side fleets/players for this client on next connect).
	// Heap: filesystem path and fstream operations for GUID persistence
	ScopedSuppressAllocationTracking suppress;

	if (!WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("ClientGuid.bin"), rGuid))
	{
		LOG(kNetwork, kError, "Failed to persist ClientGuid.bin (next session will re-handshake as a new client)");
	}
}

} // namespace

void ClientSessionBase::ConnectToServer(std::string_view serverAddress, uint16_t uiPort, int64_t iCoordSlots)
{
	// Heap: ClientNetwork allocates ENet host and peer
	ScopedSuppressAllocationTracking suppress;
	miCoordSlots = iCoordSlots;
	std::string serverAddressString(serverAddress); // null-terminate: serverAddress (string_view) is not guaranteed terminated for enet_address_set_host
	ClientGuid clientGuid = LoadClientGuidFromDisk();
	mpClientNetwork = std::make_unique<Client>(serverAddressString.c_str(), uiPort, iCoordSlots, clientGuid, &PersistClientGuidToDisk);
}

void ClientSessionBase::DisconnectFromServerBase()
{
	// Heap: ClientNetwork destructor triggers ENet disconnect and cleanup
	ScopedSuppressAllocationTracking suppress;

	miLatestServerTick = -1;
	mpClientNetwork.reset();
	mpDiscoveryScanner.reset();
	for (auto& [rCoord, rCoordFrames] : game::gpGame->mCoordFrames)
	{
		rCoordFrames.ResetClientState();
	}
	mSubscriptionQueue.clear();
	mSessionFlags.Clear(SessionStateFlags::kServerDiscovered);
	mSessionFlags.Clear(SessionStateFlags::kDiscoveryScanTimedOut);
	miClockError = 0;
	miCurrentTargetBehind = 0;
	miLastLoggedClockTargetBehind = -1;
	miLastPeriodicClockLogTick = -1;
	miLastClockErrorLogTick = -1;
	mSessionFlags.Clear(SessionStateFlags::kNoFreeSlotLogged);
}

void ClientSessionBase::StartServerDiscovery()
{
	// Heap: NetworkDiscoveryScanner creates a UDP socket
	ScopedSuppressAllocationTracking suppress;
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
		std::snprintf(mcDiscoveredAddress, sizeof(mcDiscoveredAddress), "%s", mpDiscoveryScanner->GetFoundAddress());
		mpDiscoveryScanner.reset();
		mSessionFlags.Set(SessionStateFlags::kServerDiscovered);
		return;
	}
	else if (!mpDiscoveryScanner->IsScanning())
	{
		// Timeout — restart scan
		mSessionFlags.Set(SessionStateFlags::kDiscoveryScanTimedOut);
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
			if (!(mSessionFlags & SessionStateFlags::kNoFreeSlotLogged))
			{
				mSessionFlags.Set(SessionStateFlags::kNoFreeSlotLogged);
				LOG(kNetwork, kWarning, "TrySubscribeNext NoFreeSlot (suppressing until slot frees) Pending: {} First: ({},{})", mSubscriptionQueue.size(), mSubscriptionQueue.front().x, mSubscriptionQueue.front().y);
			}
			return;
		}
		if (mSessionFlags & SessionStateFlags::kNoFreeSlotLogged)
		{
			LOG(kNetwork, kDebug, "TrySubscribeNext NoFreeSlot resolved");
		}
		mSessionFlags.Clear(SessionStateFlags::kNoFreeSlotLogged);
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

void ClientSessionBase::UnsubscribeStaleCoords(std::span<const GridCoord> desiredCoords)
{
	const std::vector<ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();

	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (!IsSlotActive(rSlots.at(i)))
		{
			continue;
		}

		if (!std::ranges::contains(desiredCoords, rSlots.at(i).coord))
		{
			GridCoord unsubCoord = rSlots.at(i).coord;
			if (rSlots.at(i).eState == CoordSubscriptionState::kSubscribing)
			{
				mpClientNetwork->CancelSubscription(i);
				game::gpGame->mCoordFrames.erase(unsubCoord);
			}
			else
			{
				mpClientNetwork->SendUnsubscribe(i);
				// rSlots aliases Client::mCoordSlots. SendUnsubscribe leaves the slot active when the connection
				// cannot send, so retain its state check before dropping the local frame data.
				if (rSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
				{
					game::gpGame->mCoordFrames.erase(unsubCoord);
				}
			}
		}
	}
}

void ClientSessionBase::BuildSubscriptionQueue(std::span<const GridCoord> desiredCoords)
{
	const std::vector<ClientCoordSlot>& rSlots = mpClientNetwork->GetCoordSlots();

	mSubscriptionQueue.clear();
	GridCoord activeCoords[NetworkManager::kiMaxEnetCoordSlots];
	int64_t iActiveCount = 0;
	for (const ClientCoordSlot& rSlot : rSlots)
	{
		if (IsSlotActive(rSlot))
		{
			activeCoords[iActiveCount++] = rSlot.coord;
		}
	}
	for (const GridCoord& rCoord : desiredCoords)
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
	ScopedSuppressAllocationTracking suppress;

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
				// Recoverable (slow client / server burst): drop the update and keep the session alive
				DEBUG_BREAK();
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

	// Sim runs BEHIND latestServerTick by miCurrentTargetBehind. Positive iError = sim is past the target;
	// normal up to kiSimCeilingSlackTicks (the ClientUpdate ceiling bounds it there), drained by gradual
	// correction. Negative iError = sim is behind the target and needs to catch up via gradual correction
	// or the snap cliff.
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
