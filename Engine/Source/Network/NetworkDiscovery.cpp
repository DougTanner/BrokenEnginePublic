#include "Pch.h"

#include "Network/NetworkDiscovery.h"

namespace engine
{

#if defined(BT_SERVER)
NetworkDiscoveryResponder::NetworkDiscoveryResponder()
{
	mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(kuiDiscoveryPort);
	addr.sin_addr.s_addr = INADDR_ANY;
	bind(mSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

	u_long uiNonBlocking = 1;
	ioctlsocket(mSocket, FIONBIO, &uiNonBlocking);
}

NetworkDiscoveryResponder::~NetworkDiscoveryResponder()
{
	closesocket(mSocket);
}

void NetworkDiscoveryResponder::Poll()
{
	sockaddr_in senderAddr {};
	int iSenderLen = sizeof(senderAddr);
	uint32_t uiMagic = 0;

	int iReceived = recvfrom(mSocket, reinterpret_cast<char*>(&uiMagic), sizeof(uiMagic), 0, reinterpret_cast<sockaddr*>(&senderAddr), &iSenderLen);
	if (iReceived == sizeof(uiMagic) && uiMagic == kuiDiscoveryMagic)
	{
		uint32_t uiResponse = kuiDiscoveryMagic;
		sendto(mSocket, reinterpret_cast<const char*>(&uiResponse), sizeof(uiResponse), 0, reinterpret_cast<sockaddr*>(&senderAddr), iSenderLen);
	}
}
#endif

#if defined(BT_CLIENT)
NetworkDiscoveryScanner::NetworkDiscoveryScanner()
{
	mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	BOOL bBroadcast = TRUE;
	setsockopt(mSocket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bBroadcast), sizeof(bBroadcast));

	u_long uiNonBlocking = 1;
	ioctlsocket(mSocket, FIONBIO, &uiNonBlocking);
}

NetworkDiscoveryScanner::~NetworkDiscoveryScanner()
{
	closesocket(mSocket);
}

void NetworkDiscoveryScanner::StartScan()
{
	uint32_t uiMagic = kuiDiscoveryMagic;

	// Send to localhost first (zero latency, always arrives first if local server exists)
	sockaddr_in localAddr {};
	localAddr.sin_family = AF_INET;
	localAddr.sin_port = htons(kuiDiscoveryPort);
	localAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sendto(mSocket, reinterpret_cast<const char*>(&uiMagic), sizeof(uiMagic), 0, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));

	// Then broadcast to LAN
	sockaddr_in broadcastAddr {};
	broadcastAddr.sin_family = AF_INET;
	broadcastAddr.sin_port = htons(kuiDiscoveryPort);
	broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	sendto(mSocket, reinterpret_cast<const char*>(&uiMagic), sizeof(uiMagic), 0, reinterpret_cast<sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));

	mTimer.Reset();
	mbStarted = true;
}

void NetworkDiscoveryScanner::Poll()
{
	sockaddr_in senderAddr {};
	int iSenderLen = sizeof(senderAddr);
	uint32_t uiMagic = 0;

	int iReceived = recvfrom(mSocket, reinterpret_cast<char*>(&uiMagic), sizeof(uiMagic), 0, reinterpret_cast<sockaddr*>(&senderAddr), &iSenderLen);
	if (iReceived == sizeof(uiMagic) && uiMagic == kuiDiscoveryMagic)
	{
		uint8_t* pBytes = reinterpret_cast<uint8_t*>(&senderAddr.sin_addr);
		snprintf(mpcFoundAddress, sizeof(mpcFoundAddress), "%u.%u.%u.%u", pBytes[0], pBytes[1], pBytes[2], pBytes[3]);
		mbFound = true;
	}
}

bool NetworkDiscoveryScanner::IsScanning()
{
	if (!mbStarted || mbFound)
	{
		return false;
	}

	return mTimer.GetDeltaNs() < std::chrono::milliseconds(kiDiscoveryScanMs);
}
#endif

} // namespace engine
