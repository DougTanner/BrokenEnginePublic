#pragma once

#include "Network/NetworkProtocol.h"

namespace engine
{

#ifdef BT_SERVER
class NetworkDiscoveryResponder
{
public:

	NetworkDiscoveryResponder();
	~NetworkDiscoveryResponder();

	void Poll();

private:

	SOCKET mSocket = INVALID_SOCKET;
};
#endif

#ifdef BT_CLIENT
class NetworkDiscoveryScanner
{
public:

	NetworkDiscoveryScanner();
	~NetworkDiscoveryScanner();

	void StartScan();
	void Poll();

	bool IsScanning();
	bool IsFound() const { return mbFound; }
	const char* GetFoundAddress() const { return mpcFoundAddress; }

private:

	SOCKET mSocket = INVALID_SOCKET;
	common::Timer mTimer;
	bool mbStarted = false;
	bool mbFound = false;
	char mpcFoundAddress[16] {};
};
#endif

} // namespace engine
