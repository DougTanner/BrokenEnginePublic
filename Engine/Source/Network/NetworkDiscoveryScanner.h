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
	bool IsFound() const { return mFlags & DiscoveryScannerFlags::kFound; }
	const char* GetFoundAddress() const { return mpcFoundAddress; }

private:

	enum class DiscoveryScannerFlags : uint8_t
	{
		kStarted = 1 << 0,
		kFound   = 1 << 1,
	};

	SOCKET mSocket = INVALID_SOCKET;
	common::Timer mTimer;
	common::Flags<DiscoveryScannerFlags> mFlags;
	char mpcFoundAddress[16] {};
};

} // namespace engine

#endif // BT_CLIENT
