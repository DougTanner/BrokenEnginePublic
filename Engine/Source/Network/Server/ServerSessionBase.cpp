#include "Pch.h"
#include "Game.h"

#if defined(BT_SERVER)

namespace engine
{

void ServerSessionBase::WaitForTick(TimeStep& rTimeStep, std::chrono::nanoseconds tickNs)
{
	// Server sleeps until next tick (there is no VSync wait), hybrid approach: waitable timer for the bulk, then spin-wait for precision
	static constexpr std::chrono::nanoseconds kSpinMarginNs = 2'000'000ns;
	std::chrono::nanoseconds remainingNs = tickNs - rTimeStep.mTickRemainderNs - rTimeStep.mRealTime.GetDeltaNs();
	std::chrono::nanoseconds sleepNs = remainingNs - kSpinMarginNs;
	if (sleepNs > 0ns)
	{
		LARGE_INTEGER dueTime {.QuadPart = -(sleepNs.count() / 100),}; // Negative = relative, 100ns units
		SetWaitableTimerEx(mTimerHandle, &dueTime, 0, nullptr, nullptr, nullptr, 0);
		WaitForSingleObject(mTimerHandle, INFINITE);
	}

	// Spin-wait
	while (rTimeStep.mRealTime.GetDeltaNs() + rTimeStep.mTickRemainderNs < tickNs)
	{
	}

	// Verify precision
	std::chrono::nanoseconds tickMarginNs = tickNs / 64;
	std::chrono::nanoseconds remainderNs = rTimeStep.mRealTime.GetDeltaNs() + rTimeStep.mTickRemainderNs - tickNs;
	static int64_t siTotalTicks = 0;
	static int64_t siOvershootTicks = 0;
	++siTotalTicks;
	if ((remainderNs < 0ns || remainderNs > tickMarginNs)) [[unlikely]]
	{
		++siOvershootTicks;
		if constexpr (kbProfilingFrameSpike)
		{
			LOG(kNetwork, kVerbose, "ServerSessionBase::WaitForTick Remainder: {}ns Overshoot: {}/{} = {}%", remainderNs.count(), siOvershootTicks, siTotalTicks, siOvershootTicks * 100 / siTotalTicks);
		}
	}
}

void ServerSessionBase::PollNetworkBase()
{
	gpServer->Poll();
	mpDiscoveryResponder->Poll();
}

void ServerSessionBase::SendNewSubscriptionFullStates([[maybe_unused]] int64_t iTick)
{
	std::vector<PendingNewSubscription>& rNewSubs = gpServer->DrainPendingNewSubscriptions();
	for (const PendingNewSubscription& rSub : rNewSubs)
	{
		const ClientConnection* pClient = gpServer->FindClient(rSub.iClientId);
		bool bSlotStillValid = (pClient != nullptr
			&& rSub.iSlot < std::ssize(pClient->coordSubscriptions)
			&& pClient->coordSubscriptions.at(rSub.iSlot).bActive
			&& pClient->coordSubscriptions.at(rSub.iSlot).coord == rSub.coord);
		if (!bSlotStillValid)
		{
			continue;
		}

		auto frameIt = game::gpGame->mCoordFrames.find(rSub.coord);
		if (frameIt != game::gpGame->mCoordFrames.end())
		{
			gpServer->SendCoordStaticData(rSub.iClientId, rSub.iSlot, rSub.coord, frameIt->second.staticData);
			gpServer->SendCoordFullState(rSub.iClientId, rSub.iSlot, game::gpGame->TickCounter(), rSub.coord, frameIt->second.pCurrent.get());
		}
	}
}

} // namespace engine

#endif // BT_SERVER
