#pragma once

#if defined(BT_SERVER)

namespace engine
{

class NetworkDiscoveryResponder
{
public:

	NetworkDiscoveryResponder();
	~NetworkDiscoveryResponder();

	void Poll();

private:

	SOCKET mSocket = INVALID_SOCKET;
};

} // namespace engine

#endif // BT_SERVER
