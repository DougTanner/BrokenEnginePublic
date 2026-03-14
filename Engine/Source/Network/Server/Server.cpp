#include "Pch.h"

#include "Network/Server/Server.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

Server::Server(uint16_t uiPort)
{
	gpServer = this;

	ENetAddress address {};
	address.host = ENET_HOST_ANY;
	address.port = uiPort;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(&address, 64, NetworkManager::kuiChannelCount, 0, 0);
	// 1MB send/receive buffers to handle bursty packet dispatches
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_SNDBUF, 1024 * 1024);
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_RCVBUF, 1024 * 1024);

	// Heap: one-time compression scratch buffer
	mCompressionBuffer.resize(kiMaxPacketSize);
}

Server::~Server()
{
	if (mpHost != nullptr)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_host_destroy(mpHost);
	}

	gpServer = nullptr;
}

void Server::Flush()
{
	enet_host_flush(mpHost);
}

void Server::Poll()
{
	if (mpHost == nullptr)
	{
		return;
	}

	mPendingSpawnRequests.clear();
	mPendingDisconnects.clear();
	mPendingNewSubscriptions.clear();

	ENetEvent event {};
	while (enet_host_service(mpHost, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
				Connect(event);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				Disconnect(event);
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
					NetworkSimulation::EnqueueOrDrop(mDelayedPackets, kSimConfig, event,
						[this](ENetEvent& rEvent) { Receive(rEvent); });
				}
				else
				{
					Receive(event);
					enet_packet_destroy(event.packet);
				}
				break;
			case ENET_EVENT_TYPE_NONE:
				break;
		}
	}

	// Process delayed packets whose release time has passed
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		NetworkSimulation::ProcessDelayed(mDelayedPackets, [this](const DelayedPacket& rPacket)
		{
			Receive(rPacket.data.data(), rPacket.data.size(), rPacket.pPeer);
		});
	}
}

void Server::Connect(ENetEvent& rEvent)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ClientConnection connection {};
	connection.pPeer = rEvent.peer;
	connection.iClientId = miNextClientId++;
	connection.coordSubscriptions.resize(NetworkManager::kiMaxEnetCoordSlots);
	connection.coordAckStates.resize(NetworkManager::kiMaxEnetCoordSlots);

	rEvent.peer->data = reinterpret_cast<void*>(connection.iClientId);

	// Heap: client vector grows on connect
	mClients.push_back(std::move(connection));

	// Disable ENet peer throttle to prevent unreliable packet drops during client reconciliation stalls
	enet_peer_throttle_configure(rEvent.peer, UINT32_MAX, 0, 0);

	Log(kLogNetwork, "Server::Connect Client: {}", mClients.back().iClientId);

	char pcAddress[64] {};
	enet_address_get_host_ip(&rEvent.peer->address, pcAddress, sizeof(pcAddress));
}

void Server::Disconnect(ENetEvent& rEvent)
{
	int64_t iClientId = reinterpret_cast<int64_t>(rEvent.peer->data);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient != nullptr)
	{
		mPendingDisconnects.push_back({iClientId, pClient->humanPlayerId, pClient->humanGridCoord});
	}
	RemoveClient(iClientId);

	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		std::erase_if(mDelayedPackets, [&rEvent](const DelayedPacket& rPacket) { return rPacket.pPeer == rEvent.peer; });
	}

	Log(kLogNetwork, "Server::Disconnect Client: {}", iClientId);

	char pcAddress[64] {};
	enet_address_get_host_ip(&rEvent.peer->address, pcAddress, sizeof(pcAddress));
}

void Server::Receive(ENetEvent& rEvent)
{
	Receive(rEvent.packet->data, rEvent.packet->dataLength, rEvent.peer);
}

void Server::Receive(const uint8_t* pData, size_t iSize, ENetPeer* pPeer)
{
	if (iSize < 1)
	{
		return;
	}

	int64_t iClientId = reinterpret_cast<int64_t>(pPeer->data);
	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
		case PacketType::kClientAckStream:
			ClientAckStream(pData, iClientId);
			break;
		case PacketType::kClientSpawnRequest:
			ClientSpawnRequest(pData, iClientId);
			break;
		case PacketType::kClientDesyncReport:
			ClientDesyncReport(pData);
			break;
		case PacketType::kClientDebugFrameRequest:
			ClientDebugFrameRequest(pData, pPeer);
			break;
		case PacketType::kClientHello:
			ClientHello(pData, iSize, pPeer, iClientId);
			break;
		case PacketType::kClientSubscribe:
			ClientSubscribe(pData, iClientId);
			break;
		case PacketType::kClientUnsubscribe:
			ClientUnsubscribe(pData, iClientId);
			break;
		default:
			break;
	}
}

void Server::BufferFrame(int64_t iTick, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates)
{
	miLatestBufferedTick = iTick;
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	for (const auto& [coord, updateData] : rGridUpdates)
	{
		// Heap: per-coord ring buffer grows until steady state
		PerCoordBufferedFrame buffered {};
		buffered.iTick = iTick;
		buffered.serverCrc = updateData.serverCrc;
		buffered.inputCrc = updateData.inputCrc;

		if (!updateData.statusChanges.empty())
		{
			int64_t iCompressedSize = CompressStatusChangeBatch(updateData.statusChanges.data(), static_cast<int64_t>(updateData.statusChanges.size()), mCompressionBuffer.data(), kiMaxPacketSize);
			buffered.compressedData.assign(mCompressionBuffer.begin(), mCompressionBuffer.begin() + iCompressedSize);
		}

		std::deque<PerCoordBufferedFrame>& rCoordBuffer = mPerCoordBufferedFrames[coord];
		rCoordBuffer.push_back(std::move(buffered));
		while (static_cast<int64_t>(rCoordBuffer.size()) > kiMaxBufferedFrames)
		{
			rCoordBuffer.pop_front();
		}
	}

	// Prune ring buffers for coords no longer in the active set
	std::erase_if(mPerCoordBufferedFrames, [&rGridUpdates](const auto& rEntry)
	{
		for (const auto& [coord, updateData] : rGridUpdates)
		{
			if (coord == rEntry.first)
			{
				return false;
			}
		}
		return true;
	});
}

void Server::BufferFullFrame(int64_t iTick, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Heap: ring buffer grows until steady state
	BufferedFullFrame buffered {};
	buffered.iTick = iTick;

	for (const auto& [coord, pFrame] : rFrames)
	{
		// Heap: stringstream allocates for frame serialization
		std::ostringstream frameStream(std::ios::binary);
		frameStream << *pFrame;
		buffered.serializedFrames[coord] = frameStream.str();
	}

	mBufferedFullFrames.push_back(std::move(buffered));
	while (static_cast<int64_t>(mBufferedFullFrames.size()) > kiMaxBufferedFrames)
	{
		mBufferedFullFrames.pop_front();
	}
}

const PerCoordBufferedFrame* Server::FindBufferedFrame(GridCoord coord, int64_t iTick) const
{
	auto coordBufferIt = mPerCoordBufferedFrames.find(coord);
	if (coordBufferIt == mPerCoordBufferedFrames.end())
	{
		return nullptr;
	}
	const std::deque<PerCoordBufferedFrame>& rCoordBuffer = coordBufferIt->second;
	if (rCoordBuffer.empty())
	{
		return nullptr;
	}
	int64_t iIndex = iTick - rCoordBuffer.front().iTick;
	if (iIndex < 0 || iIndex >= static_cast<int64_t>(rCoordBuffer.size()))
	{
		return nullptr;
	}
	return &rCoordBuffer.at(static_cast<size_t>(iIndex));
}

int Server::CompressToBuffer(const char* pData, int iSize)
{
	int iMaxCompressed = LZ4_compressBound(iSize);
	if (static_cast<int>(mCompressionBuffer.size()) < iMaxCompressed)
	{
		mCompressionBuffer.resize(iMaxCompressed);
	}
	return LZ4_compress_default(pData, reinterpret_cast<char*>(mCompressionBuffer.data()), iSize, iMaxCompressed);
}

void Server::RemoveClient(int64_t iClientId)
{
	for (size_t i = 0; i < mClients.size(); ++i)
	{
		if (mClients.at(i).iClientId == iClientId)
		{
			if (i != mClients.size() - 1)
			{
				mClients.at(i) = std::move(mClients.back());
			}
			mClients.pop_back();
			return;
		}
	}
}

ClientConnection* Server::FindClient(int64_t iClientId)
{
	for (ClientConnection& rClient : mClients)
	{
		if (rClient.iClientId == iClientId)
		{
			return &rClient;
		}
	}
	return nullptr;
}

const ClientConnection* Server::FindClient(int64_t iClientId) const
{
	for (const ClientConnection& rClient : mClients)
	{
		if (rClient.iClientId == iClientId)
		{
			return &rClient;
		}
	}
	return nullptr;
}

} // namespace engine
