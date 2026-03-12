#pragma once

#if defined(BT_CLIENT)

#include "Network/NetworkDiscovery.h"
#include "Network/NetworkClient/NetworkClient.h"

namespace engine
{

class ClientSessionBase
{
public:

	ClientSessionBase() = default;
	virtual ~ClientSessionBase() = default;

	std::unique_ptr<NetworkClient> mpNetworkClient;
	std::unique_ptr<NetworkDiscoveryScanner> mpDiscoveryScanner;
};

} // namespace engine

#endif // BT_CLIENT
