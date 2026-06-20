#include "Pch.h"

#include "Network/NetworkManager.h"

namespace engine
{

NetworkManager::NetworkManager()
{
	enet_initialize();
}

NetworkManager::~NetworkManager()
{
	enet_deinitialize();
}

} // namespace engine
