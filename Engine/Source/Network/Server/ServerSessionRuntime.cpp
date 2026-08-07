#include "Pch.h"

#include "Network/Server/ServerSessionRuntime.h"

#if defined(BT_SERVER)

#include "Network/NetworkDiscoveryResponder.h"
#include "Network/Server/Server.h"

#include "Network/Server/ServerBroadcaster.h"
#include "Network/Server/ServerSession.h"

namespace engine
{

ServerSessionRuntime::ServerSessionRuntime(game::ServerSession& rSession, uint16_t uiPort)
:	mrSession(rSession)
{
	mpDiscoveryResponder = std::make_unique<NetworkDiscoveryResponder>();
	timeBeginPeriod(1);
	mTimerHandle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	try
	{
		mpServer = std::make_unique<Server>(uiPort, *this);
	}
	catch (...)
	{
		CloseHandle(mTimerHandle);
		timeEndPeriod(1);
		throw;
	}
}

ServerSessionRuntime::~ServerSessionRuntime()
{
	mpServer.reset();
	CloseHandle(mTimerHandle);
	timeEndPeriod(1);
}

void ServerSessionRuntime::Poll(const NetworkTimeState& rTimeState)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	// Heap: ENet/discovery polling and game request queues may grow
	ScopedSuppressAllocationTracking suppress;
	mrSession.BeforeNetworkPoll();
	mpServer->Poll(rTimeState, ServerPollMode::kUpdateStart);
	mpDiscoveryResponder->Poll();
	mrSession.AfterNetworkPoll();
}

// Second poll of the update, run after WaitForTick so commands that arrived during the wait enter the
// imminent tick instead of the next one. BeforeNetworkPoll is deliberately omitted: it clears the previous
// update's pending request queues, so running it here would drop requests the update-start poll queued.
// Discovery is polled once per update by Poll above; the admission budget window opened there continues.
void ServerSessionRuntime::PollTickBoundary(const NetworkTimeState& rTimeState)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	// Heap: ENet polling and game request queues may grow
	ScopedSuppressAllocationTracking suppress;
	mpServer->Poll(rTimeState, ServerPollMode::kTickBoundary);
	mrSession.AfterNetworkPoll();
}

void ServerSessionRuntime::WaitForTick(TimeStep& rTimeStep)
{
	std::chrono::nanoseconds tickNanoseconds = rTimeStep.SimToWall(game::NetworkSessionContract::kTickDuration);
	static constexpr std::chrono::nanoseconds kSpinMarginNanoseconds = 500'000ns;
	std::chrono::nanoseconds remainingNanoseconds = tickNanoseconds - rTimeStep.mTickRemainderNs - rTimeStep.mRealTime.GetDeltaNs();
	std::chrono::nanoseconds sleepNanoseconds = remainingNanoseconds - kSpinMarginNanoseconds;
	if (sleepNanoseconds > 0ns)
	{
		LARGE_INTEGER dueTime {.QuadPart = -(sleepNanoseconds.count() / 100),};
		SetWaitableTimerEx(mTimerHandle, &dueTime, 0, nullptr, nullptr, nullptr, 0);
		WaitForSingleObject(mTimerHandle, INFINITE);
	}
	while (rTimeStep.mRealTime.GetDeltaNs() + rTimeStep.mTickRemainderNs < tickNanoseconds)
	{
		YieldProcessor();
	}

	std::chrono::nanoseconds marginNanoseconds = tickNanoseconds / 64;
	std::chrono::nanoseconds remainderNanoseconds = rTimeStep.mRealTime.GetDeltaNs() + rTimeStep.mTickRemainderNs - tickNanoseconds;
	static int64_t siTotalTicks = 0;
	static int64_t siOvershootTicks = 0;
	++siTotalTicks;
	if ((remainderNanoseconds < 0ns || remainderNanoseconds > marginNanoseconds)) [[unlikely]]
	{
		++siOvershootTicks;
		if constexpr (kbProfilingFrameSpike)
		{
			LOG(kNetwork, kVerbose, "ServerSessionRuntime::WaitForTick Remainder: {}ns Overshoot: {}/{} = {}%", remainderNanoseconds.count(), siOvershootTicks, siTotalTicks, siOvershootTicks * 100 / siTotalTicks);
		}
	}
}

void ServerSessionRuntime::CompleteTick(int64_t iTick)
{
	// Heap: client/resync/subscription bookkeeping and ENet sends outside publication construction
	ScopedSuppressAllocationTracking suppress;
	mrSession.HandleResyncRequests();
	mrSession.FinalizeTickClients();
	{
		ScopedResumeAllocationTracking resume;
		common::ScopedWorkbufferArena publicationArena = common::gpThreadLocal->mWorkbuffer.Push();
		mrSession.mpBroadcaster->BuildTickPublication(iTick, *this, publicationArena);
	}
	mrSession.mpBroadcaster->ClearBroadcastStatusChanges();
	mrSession.SubscriptionUpdates();
	mpServer->Flush();
}

void ServerSessionRuntime::CompleteUpdate(int64_t iFullTicks, int64_t iTick)
{
	// Heap: resend or paused-subscription serialization and ENet sends
	ScopedSuppressAllocationTracking suppress;
	if (iFullTicks > 0)
	{
		for (ClientConnection& rClient : mpServer->mClients)
		{
			mpServer->SendResends(rClient, iTick);
		}
		return;
	}
	mrSession.PreparePausedSubscriptions();
	mrSession.HandleResyncRequests();
	mrSession.SendNewSubscriptionFullStates();
	mpServer->Flush();
}

void ServerSessionRuntime::ResetTransportForLoad()
{
	mpServer->ClearBufferedFrames();
	mpServer->mPendingNewSubscriptions.clear();
	mpServer->mPendingResyncClientIds.clear();
	mpServer->Flush();
}

void ServerSessionRuntime::PublishTick(int64_t iTick, const std::pair<GridCoord, GridUpdateData>* pGridUpdates, int64_t iGridUpdateCount, const std::pair<GridCoord, const game::Frame*>* pFullFrames, int64_t iFullFrameCount)
{
	mpServer->BufferFrame(iTick, pGridUpdates, iGridUpdateCount);
	if (iFullFrameCount > 0)
	{
		mpServer->BufferFullFrame(iTick, pFullFrames, iFullFrameCount);
	}
	for (ClientConnection& rClient : mpServer->mClients)
	{
		mpServer->SendUpdate(rClient, iTick);
	}
}

} // namespace engine

#endif // BT_SERVER
