#include "Pch.h"

#include "Network/NetworkServer/NetworkServer.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

NetworkServer::NetworkServer(uint16_t uiPort)
{
	gpNetworkServer = this;

	ENetAddress address {};
	address.host = ENET_HOST_ANY;
	address.port = uiPort;

	common::Log("NetworkServer: Starting on port {}", uiPort); // DT: TEMP

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(&address, 64, NetworkManager::kuiChannelCount, 0, 0);
	// 1MB send/receive buffers to handle bursty packet dispatches
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_SNDBUF, 1024 * 1024);
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_RCVBUF, 1024 * 1024);

	// Heap: one-time compression scratch buffer
	mCompressionBuffer.resize(kiMaxPacketSize);
}

NetworkServer::~NetworkServer()
{
	if (mpHost != nullptr)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_host_destroy(mpHost);
	}

	gpNetworkServer = nullptr;
}

void NetworkServer::Flush()
{
	enet_host_flush(mpHost);
}

void NetworkServer::Poll()
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
				HandleConnect(event);
				break;
			case ENET_EVENT_TYPE_DISCONNECT:
				HandleDisconnect(event);
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
					bool bUnreliable = NetworkManager::IsUnreliableChannel(event.channelID);
					if (bUnreliable)
					{
						if (NetworkSimulation::ShouldDrop(kSimConfig))
						{
							enet_packet_destroy(event.packet);
							break;
						}
						ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
						// Heap: delay queue copies packet data for deferred processing
						DelayedPacket delayed {};
						delayed.releaseTime = std::chrono::steady_clock::now() + NetworkSimulation::RandomOneWayDelay(kSimConfig);
						delayed.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
						delayed.pPeer = event.peer;
						delayed.uiChannelId = event.channelID;
						auto insertPos = std::lower_bound(mDelayedPackets.begin(), mDelayedPackets.end(), delayed,
						[](const DelayedPacket& rA, const DelayedPacket& rB) { return rA.releaseTime < rB.releaseTime; });
						mDelayedPackets.insert(insertPos, std::move(delayed));
						enet_packet_destroy(event.packet);
					}
					else
					{
						HandleReceive(event);
						enet_packet_destroy(event.packet);
					}
				}
				else
				{
					HandleReceive(event);
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
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		while (!mDelayedPackets.empty() && mDelayedPackets.front().releaseTime <= now)
		{
			HandleReceive(mDelayedPackets.front().data.data(), mDelayedPackets.front().data.size(), mDelayedPackets.front().pPeer);
			mDelayedPackets.pop_front();
		}
	}
}

void NetworkServer::HandleConnect(ENetEvent& rEvent)
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

	common::Log("NetworkServer: Client {} connected", mClients.back().iClientId);

	char pcAddress[64] {};
	enet_address_get_host_ip(&rEvent.peer->address, pcAddress, sizeof(pcAddress));
	FILE_LOG(0, "[NetworkServer] Connect: clientId={} ip={} port={}", mClients.back().iClientId, pcAddress, rEvent.peer->address.port);
}

void NetworkServer::HandleDisconnect(ENetEvent& rEvent)
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

	common::Log("NetworkServer: Client {} disconnected", iClientId);

	char pcAddress[64] {};
	enet_address_get_host_ip(&rEvent.peer->address, pcAddress, sizeof(pcAddress));
	FILE_LOG(0, "[NetworkServer] Disconnect: clientId={} ip={} port={}", iClientId, pcAddress, rEvent.peer->address.port);
}

void NetworkServer::HandleReceive(ENetEvent& rEvent)
{
	HandleReceive(rEvent.packet->data, rEvent.packet->dataLength, rEvent.peer);
}

void NetworkServer::HandleReceive(const uint8_t* pData, size_t iSize, ENetPeer* pPeer)
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
			HandleClientAckStream(pData, iClientId);
			break;
		case PacketType::kClientSpawnRequest:
			HandleClientSpawnRequest(pData, iClientId);
			break;
		case PacketType::kClientDesyncReport:
			HandleClientDesyncReport(pData);
			break;
		case PacketType::kClientDebugFrameRequest:
			HandleClientDebugFrameRequest(pData, pPeer);
			break;
		case PacketType::kClientHello:
			HandleClientHello(pData, iSize, pPeer, iClientId);
			break;
		case PacketType::kClientSubscribe:
			HandleClientSubscribe(pData, iClientId);
			break;
		case PacketType::kClientUnsubscribe:
			HandleClientUnsubscribe(pData, iClientId);
			break;
		default:
			break;
	}
}

void NetworkServer::BufferFrame(int64_t iTick, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates)
{
	common::Log("NetworkServer: BufferFrame tick {} coords={}", iTick, rGridUpdates.size()); // DT: TEMP
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

void NetworkServer::BufferFullFrame(int64_t iTick, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames)
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

const PerCoordBufferedFrame* NetworkServer::FindBufferedFrame(GridCoord coord, int64_t iTick) const
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
	return &rCoordBuffer[static_cast<size_t>(iIndex)];
}

int NetworkServer::CompressToBuffer(const char* pData, int iSize)
{
	int iMaxCompressed = LZ4_compressBound(iSize);
	if (static_cast<int>(mCompressionBuffer.size()) < iMaxCompressed)
	{
		mCompressionBuffer.resize(iMaxCompressed);
	}
	return LZ4_compress_default(pData, reinterpret_cast<char*>(mCompressionBuffer.data()), iSize, iMaxCompressed);
}

void NetworkServer::RemoveClient(int64_t iClientId)
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

ClientConnection* NetworkServer::FindClient(int64_t iClientId)
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

const ClientConnection* NetworkServer::FindClient(int64_t iClientId) const
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
