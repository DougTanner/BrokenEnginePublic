#pragma once

#if defined(BT_CLIENT)

namespace engine
{

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

} // namespace engine

#endif // BT_CLIENT
