#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/Server/Server.h"

#include "Game.h"

namespace engine
{

Server::Server(uint16_t uiPort)
{
	gpServer = this;

	ENetAddress address {};
	address.host = ENET_HOST_ANY;
	address.port = uiPort;

	ScopedSuppressAllocationTracking suppress;
	// Heap: one-time compression scratch buffer
	mCompressionBuffer.resize(kiMaxPacketSize);
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(&address, 64, NetworkManager::kuiChannelCount, 0, 0);
	if (mpHost == nullptr)
	{
		LOG(kNetwork, kWarning, "Server::Server enet_host_create failed");
		return;
	}
	// 1MB send/receive buffers to handle bursty packet dispatches
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_SNDBUF, 1024 * 1024);
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_RCVBUF, 1024 * 1024);
}

Server::~Server()
{
	if (mpHost != nullptr)
	{
		// Heap: ENet destroys host data internally
		ScopedSuppressAllocationTracking suppress;
		enet_host_destroy(mpHost);
	}

	gpServer = nullptr;
}

void Server::Flush()
{
	if (mpHost == nullptr)
	{
		return;
	}
	enet_host_flush(mpHost);
}

void Server::Poll()
{
	ASSERT(common::gpMultithreading->IsMainThread());

	if (mpHost == nullptr)
	{
		return;
	}

	mPendingSpawnRequests.clear();
	mPendingDisconnects.clear();
	mPendingNewSubscriptions.clear();
	mPendingResyncClientIds.clear();
	mReceivedGamePackets.clear();

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
				DispatchIncoming(event);
				break;
			case ENET_EVENT_TYPE_NONE:
				break;
		}
	}

	// Process delayed packets whose release time has passed (or flush all when bypassing simulation)
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		auto handleDelayed = [this](const DelayedPacket& rPacket)
		{
			Receive(rPacket.data.data(), rPacket.data.size(), rPacket.pPeer);
		};
		if (game::gpGame->mTimeStep.miTimeMultiply > 1)
		{
			NetworkSimulation::FlushDelayed(mDelayedPackets, handleDelayed);
		}
		else
		{
			NetworkSimulation::ProcessDelayed(mDelayedPackets, handleDelayed);
		}
	}
}

void Server::DispatchIncoming(ENetEvent& rEvent)
{
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		if (game::gpGame->mTimeStep.miTimeMultiply > 1)
		{
			Receive(rEvent);
			enet_packet_destroy(rEvent.packet);
		}
		else
		{
			constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
			NetworkSimulation::EnqueueOrDrop(mDelayedPackets, kSimConfig, rEvent,
				[this](ENetEvent& rInner) { Receive(rInner); });
		}
	}
	else
	{
		Receive(rEvent);
		enet_packet_destroy(rEvent.packet);
	}
}

void Server::Connect(ENetEvent& rEvent)
{
	ScopedSuppressAllocationTracking suppress;

	ClientConnection connection {};
	connection.pPeer = rEvent.peer;
	connection.iClientId = miNextClientId++;
	connection.coordSubscriptions.resize(NetworkManager::kiMaxEnetCoordSlots);
	connection.coordAckStates.resize(NetworkManager::kiMaxEnetCoordSlots);
	connection.prevResendCounts.resize(NetworkManager::kiMaxEnetCoordSlots, 0);
	connection.resendLogCooldowns.resize(NetworkManager::kiMaxEnetCoordSlots, 0);

	rEvent.peer->data = reinterpret_cast<void*>(connection.iClientId);

	// Heap: client vector grows on connect
	mClients.push_back(std::move(connection));

	// Disable ENet peer throttle to prevent unreliable packet drops during client reconciliation stalls
	enet_peer_throttle_configure(rEvent.peer, UINT32_MAX, 0, 0);

	LOG(kNetwork, kInfo, "Server::Connect Client: {}", mClients.back().iClientId);
}

void Server::Disconnect(ENetEvent& rEvent)
{
	int64_t iClientId = reinterpret_cast<int64_t>(rEvent.peer->data);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient != nullptr)
	{
		mPendingDisconnects.push_back({iClientId, pClient->clientGuid});
	}
	RemoveClient(iClientId);

	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		std::erase_if(mDelayedPackets, [&rEvent](const DelayedPacket& rPacket) { return rPacket.pPeer == rEvent.peer; });
	}

	LOG(kNetwork, kInfo, "Server::Disconnect Client: {}", iClientId);
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
			ClientAckStream(pData, iSize, iClientId);
			break;
		case PacketType::kClientSpawnRequest:
			ClientSpawnRequest(pData, iSize, iClientId);
			break;
		case PacketType::kClientDesyncReport:
			ClientDesyncReport(pData, iSize, iClientId);
			break;
		case PacketType::kClientDebugFrameRequest:
			ClientDebugFrameRequest(pData, iSize, pPeer, iClientId);
			break;
		case PacketType::kClientHello:
			ClientHello(pData, iSize, pPeer, iClientId);
			break;
		case PacketType::kClientSubscribe:
			ClientSubscribe(pData, iSize, iClientId);
			break;
		case PacketType::kClientUnsubscribe:
			ClientUnsubscribe(pData, iSize, iClientId);
			break;
		case PacketType::kClientResyncRequest:
			ClientResyncRequest(iClientId);
			break;
		default:
			if (static_cast<uint8_t>(eType) >= static_cast<uint8_t>(PacketType::kGamePacketStart))
			{
				ClientConnection* pClient = FindHandshakenClient(iClientId);
				if (pClient == nullptr)
				{
					break;
				}
				ScopedSuppressAllocationTracking suppress;
				// Heap: raw game packet buffer grows on game-specific packets
				mReceivedGamePackets.push_back({iClientId, pData[0], std::vector<uint8_t>(pData + 1, pData + iSize)});
			}
			else
			{
				LOG(kNetwork, kWarning, "Server::Receive unknown packet type {} Client: {}", static_cast<uint8_t>(eType), iClientId);
			}
			break;
	}
}

void Server::BufferFrame(int64_t iTick, std::span<const std::pair<GridCoord, GridUpdateData>> gridUpdates)
{
	miLatestBufferedTick = iTick;
	ScopedSuppressAllocationTracking suppress;

	std::unordered_set<GridCoord> activeCoords;
	activeCoords.reserve(gridUpdates.size());

	for (const std::pair<GridCoord, GridUpdateData>& rGridUpdate : gridUpdates)
	{
		const GridCoord& rCoord = rGridUpdate.first;
		const GridUpdateData& rUpdateData = rGridUpdate.second;
		activeCoords.insert(rCoord);

		// Heap: per-coord ring buffer grows until steady state
		PerCoordBufferedFrame buffered {};
		buffered.iTick = iTick;
		buffered.sharedCrc = rUpdateData.sharedCrc;

		if (!rUpdateData.statusChanges.empty())
		{
			int64_t iCompressedSize = CompressStatusChangeBatch(rUpdateData.statusChanges.data(), static_cast<int64_t>(rUpdateData.statusChanges.size()), mCompressionBuffer.data(), kiMaxPacketSize);
			buffered.compressedData.assign(mCompressionBuffer.begin(), mCompressionBuffer.begin() + iCompressedSize);
		}

		std::deque<PerCoordBufferedFrame>& rCoordBuffer = mPerCoordBufferedFrames.try_emplace(rCoord).first->second;
		rCoordBuffer.push_back(std::move(buffered));
		while (static_cast<int64_t>(rCoordBuffer.size()) > kiMaxBufferedFrames)
		{
			rCoordBuffer.pop_front();
		}
	}

	// Prune ring buffers for coords no longer in the active set
	std::erase_if(mPerCoordBufferedFrames, [&activeCoords](const std::pair<const GridCoord, std::deque<PerCoordBufferedFrame>>& rEntry)
	{
		return !activeCoords.contains(rEntry.first);
	});
}

void Server::BufferFullFrame(int64_t iTick, std::span<const std::pair<GridCoord, const game::Frame*>> frames)
{
	ScopedSuppressAllocationTracking suppress;

	// Heap: ring buffer grows until steady state
	BufferedFullFrame buffered {};
	buffered.iTick = iTick;

	for (const std::pair<GridCoord, const game::Frame*>& rFrame : frames)
	{
		// Heap: stringstream allocates for frame serialization
		std::ostringstream frameStream(std::ios::binary);
		frameStream << *rFrame.second;
		buffered.serializedFrames.insert_or_assign(rFrame.first, frameStream.str());
	}

	mBufferedFullFrames.push_back(std::move(buffered));
	while (static_cast<int64_t>(mBufferedFullFrames.size()) > kiMaxBufferedFrames)
	{
		mBufferedFullFrames.pop_front();
	}
}

void Server::ClearBufferedFrames()
{
	mPerCoordBufferedFrames.clear();
	mBufferedFullFrames.clear();
	miLatestBufferedTick = -1;
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

ClientConnection* Server::FindHandshakenClient(int64_t iClientId)
{
	ClientConnection* pClient = FindClient(iClientId);
	return (pClient != nullptr && pClient->bHandshakeComplete) ? pClient : nullptr;
}

} // namespace engine

#endif // BT_SERVER
