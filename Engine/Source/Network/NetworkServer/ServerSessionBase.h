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
	}

	virtual ~ServerSessionBase() = default;

	std::unique_ptr<NetworkDiscoveryResponder> mpDiscoveryResponder;
};
#endif // BT_SERVER

} // namespace engine
