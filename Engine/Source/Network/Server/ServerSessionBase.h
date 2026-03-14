#pragma once

namespace engine
{

class NetworkDiscoveryResponder;

#if defined(BT_SERVER)
class ServerSessionBase
{
public:

	ServerSessionBase()
	{
		mpDiscoveryResponder = std::make_unique<NetworkDiscoveryResponder>();
		timeBeginPeriod(1);
		mTimerHandle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	}

	virtual ~ServerSessionBase()
	{
		CloseHandle(mTimerHandle);
		timeEndPeriod(1);
	}

	// Tick timing
	void WaitForTick(TimeStep& rTimeStep, std::chrono::nanoseconds tickNs);

	// Network polling
	void PollNetworkBase();

	// Subscription management
	void SendNewSubscriptionFullStates(int64_t iTick);

	std::unique_ptr<NetworkDiscoveryResponder> mpDiscoveryResponder;
	HANDLE mTimerHandle = nullptr;
};
#endif // BT_SERVER

} // namespace engine
