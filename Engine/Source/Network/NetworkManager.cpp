#include "Pch.h"

#include "Network/NetworkManager.h"

namespace engine
{

NetworkManager::NetworkManager()
{
	if (enet_initialize() != 0)
	{
		LOG(kNetwork, kError, "NetworkManager::NetworkManager enet_initialize failed");
	}
}

NetworkManager::~NetworkManager()
{
	enet_deinitialize();
}

} // namespace engine
