#include "Pch.h"

#include "Network/NetworkManager.h"

namespace engine
{

NetworkManager::NetworkManager()
{
	ASSERT(gpNetworkManager == nullptr);

	gpNetworkManager = this;

	enet_initialize();
}

NetworkManager::~NetworkManager()
{
	enet_deinitialize();

	if (gpNetworkManager == this)
	{
		gpNetworkManager = nullptr;
	}
}

} // namespace engine
