#include "Pch.h"

#if defined(BT_CLIENT)

#include "Network/NetworkDiscoveryScanner.h"

namespace engine
{

NetworkDiscoveryScanner::NetworkDiscoveryScanner()
{
	mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mSocket == INVALID_SOCKET)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryScanner socket creation failed: {}", WSAGetLastError());
	}

	BOOL bBroadcast = TRUE;
	if (setsockopt(mSocket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bBroadcast), sizeof(bBroadcast)) == SOCKET_ERROR)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryScanner setsockopt SO_BROADCAST failed: {}", WSAGetLastError());
	}

	u_long uiNonBlocking = 1;
	if (ioctlsocket(mSocket, FIONBIO, &uiNonBlocking) == SOCKET_ERROR)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryScanner ioctlsocket failed: {}", WSAGetLastError());
	}
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
	mFlags.Set(DiscoveryScannerFlags::kStarted);
}

void NetworkDiscoveryScanner::Poll()
{
	ASSERT(common::gpMultithreading->IsMainThread());

	sockaddr_in senderAddr {};
	int iSenderLen = sizeof(senderAddr);
	uint32_t uiMagic = 0;

	int iReceived = recvfrom(mSocket, reinterpret_cast<char*>(&uiMagic), sizeof(uiMagic), 0, reinterpret_cast<sockaddr*>(&senderAddr), &iSenderLen);
	if (iReceived == sizeof(uiMagic) && uiMagic == kuiDiscoveryMagic)
	{
		uint8_t* pBytes = reinterpret_cast<uint8_t*>(&senderAddr.sin_addr);
		std::snprintf(mpcFoundAddress, sizeof(mpcFoundAddress), "%u.%u.%u.%u", pBytes[0], pBytes[1], pBytes[2], pBytes[3]);
		mFlags.Set(DiscoveryScannerFlags::kFound);
	}
}

bool NetworkDiscoveryScanner::IsScanning()
{
	if (!(mFlags & DiscoveryScannerFlags::kStarted) || (mFlags & DiscoveryScannerFlags::kFound))
	{
		return false;
	}

	return mTimer.GetDeltaNs() < kDiscoveryScanDuration;
}

} // namespace engine

#endif // BT_CLIENT
