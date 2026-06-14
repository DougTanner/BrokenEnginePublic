#pragma once

namespace engine
{

class NetworkDiscoveryResponder;

#if defined(BT_SERVER)
class ServerSessionBase
{
public:

	ServerSessionBase();
	virtual ~ServerSessionBase();

	// Tick timing
	void WaitForTick(TimeStep& rTimeStep, std::chrono::nanoseconds tickNs);

	// Network polling
	void PollNetworkBase();

	// Subscription management
	void SendNewSubscriptionFullStates();

	std::unique_ptr<NetworkDiscoveryResponder> mpDiscoveryResponder;
	HANDLE mTimerHandle = nullptr;
};
#endif // BT_SERVER

} // namespace engine
