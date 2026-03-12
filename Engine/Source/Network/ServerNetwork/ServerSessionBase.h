#pragma once

#include "Network/NetworkDiscovery.h"

namespace engine
{

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

	std::unique_ptr<NetworkDiscoveryResponder> mpDiscoveryResponder;
	HANDLE mTimerHandle = nullptr;
};
#endif // BT_SERVER

} // namespace engine
