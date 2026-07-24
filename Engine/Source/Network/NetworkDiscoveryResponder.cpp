#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/NetworkDiscoveryResponder.h"

namespace engine
{

NetworkDiscoveryResponder::NetworkDiscoveryResponder()
{
	mSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mSocket == INVALID_SOCKET)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryResponder socket creation failed: {}", WSAGetLastError());
	}

	sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(kuiDiscoveryPort);
	addr.sin_addr.s_addr = (gLaunchOptions.flags & LaunchOptionFlags::kLoopbackOnly) ? htonl(INADDR_LOOPBACK) : INADDR_ANY;
	if (bind(mSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryResponder bind failed: {}", WSAGetLastError());
	}

	u_long uiNonBlocking = 1;
	if (ioctlsocket(mSocket, FIONBIO, &uiNonBlocking) == SOCKET_ERROR)
	{
		LOG(kNetwork, kError, "NetworkDiscoveryResponder ioctlsocket failed: {}", WSAGetLastError());
	}
}

NetworkDiscoveryResponder::~NetworkDiscoveryResponder()
{
	closesocket(mSocket);
}

void NetworkDiscoveryResponder::Poll()
{
	ASSERT(common::gpMultithreading->IsMainThread());

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

} // namespace engine

#endif // BT_SERVER
