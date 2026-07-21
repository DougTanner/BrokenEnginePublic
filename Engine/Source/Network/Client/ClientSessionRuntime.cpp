#include "Pch.h"

#include "Network/Client/ClientSessionRuntime.h"

#if defined(BT_CLIENT)

#include "Network/Client/Client.h"
#include "Network/NetworkDiscoveryScanner.h"

#include "Network/Client/ClientSession.h"
#include "Profile/ProfileManager.h"

namespace engine
{

namespace
{

ClientGuid LoadClientGuidFromDisk()
{
	// Heap: filesystem path and file stream operations for GUID persistence
	ScopedSuppressAllocationTracking suppress;
	ClientGuid loadedGuid {};
	if (ReadVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("ClientGuid.bin"), loadedGuid) && !loadedGuid.IsEmpty())
	{
		LOG(kNetwork, kInfo, "ClientSessionRuntime loaded GUID from disk: {} {}", loadedGuid.uiHigh, loadedGuid.uiLow);
		return loadedGuid;
	}
	return {};
}

void PersistClientGuidToDisk(const ClientGuid& rGuid)
{
	// Heap: filesystem path and file stream operations for GUID persistence
	ScopedSuppressAllocationTracking suppress;
	if (!WriteVersionedFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("ClientGuid.bin"), rGuid))
	{
		LOG(kNetwork, kError, "Failed to persist ClientGuid.bin (next session will re-handshake as a new client)");
	}
}

bool IsSlotActive(const ClientCoordSlot& rSlot)
{
	return rSlot.eState != CoordSubscriptionState::kUnsubscribed && rSlot.eState != CoordSubscriptionState::kUnsubscribing;
}

bool ContainsCoordinate(const GridCoord* pCoordinates, int64_t iCount, GridCoord coordinate)
{
	for (int64_t i = 0; i < iCount; ++i)
	{
		if (pCoordinates[i] == coordinate)
		{
			return true;
		}
	}
	return false;
}

} // namespace

ClientSessionRuntime::ClientSessionRuntime(game::ClientSession& rSession)
:	mrSession(rSession)
{
}

ClientSessionRuntime::~ClientSessionRuntime() = default;

void ClientSessionRuntime::ResetClock()
{
	miLatestServerTick = -1;
	miClockError = 0;
	miClockOffset = 0;
	miClockTargetBehind = 0;
	miCurrentTargetBehind = 0;
	miLastLoggedClockTargetBehind = -1;
	miLastPeriodicClockLogTick = -1;
	miLastClockErrorLogTick = -1;
}

void ClientSessionRuntime::ResetForConnect()
{
	ResetClock();
	ClearSubscriptionState();
	mpDiscoveryScanner.reset();
	mStateFlags.Clear(ClientSessionStateFlags::kServerDiscovered);
	mStateFlags.Clear(ClientSessionStateFlags::kDiscoveryScanTimedOut);
	mStateFlags.Clear(ClientSessionStateFlags::kNoFreeSlotLogged);
}

void ClientSessionRuntime::Connect(std::string_view serverAddress, uint16_t uiPort, int64_t iCoordSlots)
{
	// Heap: address copy, GUID file I/O, and Client/ENet construction
	ScopedSuppressAllocationTracking suppress;
	ResetForConnect();
	std::string serverAddressString(serverAddress);
	ClientGuid clientGuid = LoadClientGuidFromDisk();
	mpClient = std::make_unique<Client>(serverAddressString.c_str(), uiPort, iCoordSlots, clientGuid, &PersistClientGuidToDisk);
}

void ClientSessionRuntime::ConnectToDiscoveredServer(uint16_t uiPort, int64_t iCoordSlots)
{
	mStateFlags.Clear(ClientSessionStateFlags::kServerDiscovered);
	Connect(mcDiscoveredAddress, uiPort, iCoordSlots);
}

void ClientSessionRuntime::Disconnect()
{
	// Heap: Client/ENet and discovery teardown may allocate during cleanup
	ScopedSuppressAllocationTracking suppress;
	mpClient.reset();
	mpDiscoveryScanner.reset();
	ResetClock();
	ClearSubscriptionState();
	mStateFlags.Clear(ClientSessionStateFlags::kServerDiscovered);
	mStateFlags.Clear(ClientSessionStateFlags::kDiscoveryScanTimedOut);
	mStateFlags.Clear(ClientSessionStateFlags::kNoFreeSlotLogged);
	mrSession.OnRuntimeDisconnected();
}

void ClientSessionRuntime::StartDiscovery()
{
	// Heap: discovery scanner and UDP socket construction
	ScopedSuppressAllocationTracking suppress;
	mpDiscoveryScanner = std::make_unique<NetworkDiscoveryScanner>();
	mpDiscoveryScanner->StartScan();
}

void ClientSessionRuntime::PollDiscovery()
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
		mStateFlags.Set(ClientSessionStateFlags::kServerDiscovered);
	}
	else if (!mpDiscoveryScanner->IsScanning())
	{
		mStateFlags.Set(ClientSessionStateFlags::kDiscoveryScanTimedOut);
		mpDiscoveryScanner.reset();
		StartDiscovery();
	}
}

void ClientSessionRuntime::ResetForServerLoad()
{
	ResetClock();
	ClearSubscriptionState();
	mpClient->ResetAllSlots();
	mpClient->mReceivedFullStates.clear();
	for (std::vector<ReceivedCoordUpdate>& rSlotUpdates : mpClient->mReceivedCoordUpdates)
	{
		rSlotUpdates.clear();
	}
}

void ClientSessionRuntime::PollAndDrain(const NetworkTimeState& rTimeState)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	PollDiscovery();
	if (mpClient == nullptr)
	{
		return;
	}

	mpClient->Poll(rTimeState);
	if (!(mpClient->mStateFlags & Client::ClientStateFlags::kConnectionAccepted))
	{
		if (const char* pcRejection = mpClient->mpcRejectionReason[0] != '\0' ? mpClient->mpcRejectionReason : nullptr; pcRejection != nullptr)
		{
			mrSession.OnConnectionRejected(pcRejection);
			Disconnect();
		}
		else if (mpClient->mStateFlags & Client::ClientStateFlags::kDisconnectedEvent)
		{
			mrSession.OnConnectionFailed();
			Disconnect();
		}
		else
		{
			SendAckAndFlush();
		}
		return;
	}

	mrSession.OnConnectionAccepted();
	mrSession.PollDesyncState();
	if (mpClient == nullptr)
	{
		return;
	}
	if (mpClient->mStateFlags & Client::ClientStateFlags::kDisconnectedEvent)
	{
		mrSession.OnConnectionLost();
		if (mpClient != nullptr)
		{
			Disconnect();
		}
		return;
	}

	if (mpClient->DrainLoadNotification())
	{
		ResetForServerLoad();
		mrSession.OnServerLoad();
	}
	mrSession.ProcessReceivedGamePackets();
	mrSession.ApplyReceivedStaticData();
	mrSession.ApplyReceivedFullStates();
	(void)mrSession.ApplyReceivedUpdates();

	SendAckAndFlush();
}

void ClientSessionRuntime::SendAckAndFlush()
{
	gpProfileManager->CpuStart(kCpuTimerNetworkSend);
	if (mpClient->SendAck())
	{
		mpClient->Flush();
	}
	gpProfileManager->CpuStop(kCpuTimerNetworkSend, CpuStopFlags::kSmoothNow);
}

void ClientSessionRuntime::SetDesiredCoords(const GridCoord* pDesiredCoords, int64_t iDesiredCount, std::string_view reason, int64_t iTick)
{
	bool bChanged = iDesiredCount != std::ssize(mDesiredCoords);
	for (int64_t i = 0; !bChanged && i < iDesiredCount; ++i)
	{
		bChanged = pDesiredCoords[i] != mDesiredCoords.at(i);
	}
	if (!bChanged)
	{
		return;
	}

	// Heap: sticky-coordinate map insertion and desired-coordinate vector assignment
	ScopedSuppressAllocationTracking suppress;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	for (const GridCoord& rCoord : mDesiredCoords)
	{
		if (!ContainsCoordinate(pDesiredCoords, iDesiredCount, rCoord))
		{
			mUnwantedTimestamps.try_emplace(rCoord, now);
		}
	}
	for (int64_t i = 0; i < iDesiredCount; ++i)
	{
		mUnwantedTimestamps.erase(pDesiredCoords[i]);
	}
	mDesiredCoords.clear();
	if (iDesiredCount > 0)
	{
		mDesiredCoords.assign(pDesiredCoords, pDesiredCoords + iDesiredCount);
	}
	LOG(kNetwork, kVerbose, "Desired subscriptions changed Reason: {} Count: {} Tick: {}", reason, iDesiredCount, iTick);
}

void ClientSessionRuntime::SynchronizeSubscriptions()
{
	if (mpClient == nullptr)
	{
		return;
	}
	// Heap: subscription queue growth and ENet subscription sends
	ScopedSuppressAllocationTracking suppress;
	common::ScopedWorkbufferArena desiredArena = common::gpThreadLocal->mWorkbuffer.Push();
	for (const GridCoord& rCoord : mDesiredCoords)
	{
		desiredArena.PushBack(rCoord);
	}
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::erase_if(mUnwantedTimestamps, [&](const auto& rPair)
	{
		if (now - rPair.second >= kStickySubscriptionDuration)
		{
			return true;
		}
		desiredArena.PushBack(rPair.first);
		return false;
	});
	const GridCoord* pDesiredCoords = desiredArena.Data<GridCoord>();
	int64_t iDesiredCount = desiredArena.Count<GridCoord>();
	UnsubscribeStaleCoords(pDesiredCoords, iDesiredCount);
	mpClient->RecoverTimedOutSubscriptions();
	BuildSubscriptionQueue(pDesiredCoords, iDesiredCount);
	TrySubscribeNext();
}

void ClientSessionRuntime::UnsubscribeStaleCoords(const GridCoord* pDesiredCoords, int64_t iDesiredCount)
{
	const std::vector<ClientCoordSlot>& rSlots = mpClient->mCoordSlots;
	for (int64_t i = 0; i < std::ssize(rSlots); ++i)
	{
		if (!IsSlotActive(rSlots.at(i)) || ContainsCoordinate(pDesiredCoords, iDesiredCount, rSlots.at(i).coord))
		{
			continue;
		}
		GridCoord coord = rSlots.at(i).coord;
		if (rSlots.at(i).eState == CoordSubscriptionState::kSubscribing)
		{
			mpClient->CancelSubscription(i);
			mrSession.OnCoordReleased(coord);
		}
		else
		{
			mpClient->SendUnsubscribe(i);
			if (rSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
			{
				mrSession.OnCoordReleased(coord);
			}
		}
	}
}

void ClientSessionRuntime::BuildSubscriptionQueue(const GridCoord* pDesiredCoords, int64_t iDesiredCount)
{
	mSubscriptionQueue.clear();
	const std::vector<ClientCoordSlot>& rSlots = mpClient->mCoordSlots;
	for (int64_t i = 0; i < iDesiredCount; ++i)
	{
		const GridCoord& rCoord = pDesiredCoords[i];
		bool bActive = std::ranges::any_of(rSlots, [&](const ClientCoordSlot& rSlot)
		{
			return IsSlotActive(rSlot) && rSlot.coord == rCoord;
		});
		if (!bActive)
		{
			mSubscriptionQueue.push_back(rCoord);
		}
	}
}

void ClientSessionRuntime::TrySubscribeNext()
{
	if (!(mpClient->mStateFlags & Client::ClientStateFlags::kConnected))
	{
		return;
	}
	while (!mSubscriptionQueue.empty())
	{
		if (!mpClient->SendSubscribe(mSubscriptionQueue.front()))
		{
			if (!(mStateFlags & ClientSessionStateFlags::kNoFreeSlotLogged))
			{
				mStateFlags.Set(ClientSessionStateFlags::kNoFreeSlotLogged);
				LOG(kNetwork, kWarning, "ClientSessionRuntime no free subscription slot Pending: {}", mSubscriptionQueue.size());
			}
			return;
		}
		mSubscriptionQueue.erase(mSubscriptionQueue.begin());
	}
	mStateFlags.Clear(ClientSessionStateFlags::kNoFreeSlotLogged);
}

void ClientSessionRuntime::ClearSubscriptionState()
{
	mDesiredCoords.clear();
	mUnwantedTimestamps.clear();
	mSubscriptionQueue.clear();
}

std::chrono::nanoseconds ClientSessionRuntime::EvaluateClock(int64_t iPreReconcileTick)
{
	if (miLatestServerTick < 0 || mpClient == nullptr)
	{
		return 0ns;
	}
	bool bHasActiveSlot = std::ranges::any_of(mpClient->mCoordSlots, [](const ClientCoordSlot& rSlot)
	{
		return rSlot.eState == CoordSubscriptionState::kActive;
	});
	if (!bHasActiveSlot)
	{
		miLatestServerTick = -1;
		return 0ns;
	}
	int64_t iJitterMicroseconds = mpClient->mSmoothedJitterUs.Get();
	int64_t iTickMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(game::NetworkSessionContract::kTickDuration).count();
	int64_t iComputedTargetBehind = (iJitterMicroseconds + kiJitterSafetyUs + iTickMicroseconds - 1) / iTickMicroseconds;
	if (miCurrentTargetBehind == 0 || std::abs(iComputedTargetBehind - miCurrentTargetBehind) >= 2)
	{
		miCurrentTargetBehind = iComputedTargetBehind;
	}
	miClockTargetBehind = miCurrentTargetBehind;
	miClockOffset = iPreReconcileTick - miLatestServerTick;
	miClockError = iPreReconcileTick - (miLatestServerTick - miCurrentTargetBehind);
	bool bPeriodic = iPreReconcileTick % (kiTickRate * 32) == 0 && iPreReconcileTick != miLastPeriodicClockLogTick;
	bool bError = std::abs(miClockError) >= 4 && iPreReconcileTick != miLastClockErrorLogTick;
	if (miCurrentTargetBehind != miLastLoggedClockTargetBehind || bPeriodic || bError)
	{
		LOG(kNetwork, kVerbose, "ClockSync TargetBehind: {} Error: {} Offset: {} JitterUs: {} LatestServer: {} SimTick: {}", miCurrentTargetBehind, miClockError, miClockOffset, iJitterMicroseconds, miLatestServerTick, iPreReconcileTick);
		miLastLoggedClockTargetBehind = miCurrentTargetBehind;
		if (bPeriodic)
		{
			miLastPeriodicClockLogTick = iPreReconcileTick;
		}
		if (bError)
		{
			miLastClockErrorLogTick = iPreReconcileTick;
		}
	}
	if (std::abs(miClockError) < 4)
	{
		miLastClockErrorLogTick = -1;
	}
	int64_t iSteps = std::clamp(miClockError, -4LL, 4LL);
	int64_t iDivisor = std::abs(miClockError) >= 4 ? 8 : 64;
	return std::chrono::nanoseconds(-iSteps * game::NetworkSessionContract::kTickDuration.count() / iDivisor);
}

} // namespace engine

#endif // BT_CLIENT
