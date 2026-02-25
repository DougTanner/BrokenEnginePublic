#include "Pch.h"

#include "Network/NetworkManager.h"

namespace engine
{

NetworkManager::NetworkManager()
{
	gpNetworkManager = this;

	enet_initialize();
}

NetworkManager::~NetworkManager()
{
	enet_deinitialize();

	gpNetworkManager = nullptr;
}

} // namespace engine
