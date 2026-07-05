#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/Server/Server.h"

#include "Game.h"

namespace engine
{

Server::Server(uint16_t uiPort)
{
	ASSERT(gpServer == nullptr);

	gpServer = this;

	ENetAddress address {};
	address.host = ENET_HOST_ANY;
	address.port = uiPort;

	ScopedSuppressAllocationTracking suppress;
	// Heap: one-time compression scratch buffer, sized so any valid capped StatusChange batch always fits
	// (CompressToBuffer grows it further on demand for full debug frames)
	mCompressionBuffer.resize(kiMaxCompressedStatusChangeBatchBytes);
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

	if (gpServer == this)
	{
		gpServer = nullptr;
	}
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
	// mPendingNewSubscriptions / mPendingResyncClientIds are intentionally NOT cleared here. Their consumers
	// (ServerSessionBase::SendNewSubscriptionFullStates / game ServerSession::HandleResyncRequests) run only
	// post-tick, so a per-poll clear would drop a subscribe/resync accepted while the server is paused
	// (iFullTicks == 0) before any full state is sent. They persist until those consumers service and clear them.
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
		bool bFastForward = game::gpGame->mTimeStep.miTimeMultiply > 1;
		NetworkSimulation::ProcessOrFlush(mDelayedPackets, bFastForward,
			[this](const DelayedPacket& rPacket) { Receive(rPacket.data.data(), rPacket.data.size(), rPacket.pPeer); });
	}
}

void Server::DispatchIncoming(ENetEvent& rEvent)
{
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
		bool bFastForward = game::gpGame->mTimeStep.miTimeMultiply > 1;
		NetworkSimulation::DispatchOrEnqueue(mDelayedPackets, mNetworkSimState, kSimConfig, bFastForward, rEvent,
			[this](ENetEvent& rInner) { Receive(rInner); });
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

	try
	{
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
	catch (const std::exception& rException)
	{
		// Trust boundary: a corrupt count/size in a received payload throws CorruptStreamException
		// (or .at()/bad_alloc) from the reader before any client state is mutated (handlers land
		// parsed values in locals first). Drop the single packet and let the client resend/reconnect,
		// rather than tearing down the peer.
		LOG(kNetwork, kWarning, "Server::Receive dropped corrupt packet (type {}) Client: {}: {}", static_cast<uint8_t>(eType), iClientId, rException.what());
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
			int64_t iStatusChangeCount = static_cast<int64_t>(rUpdateData.statusChanges.size());
			if (iStatusChangeCount > kiMaxStatusChangesPerCell)
			{
				// Should never happen: the sim must not exceed the protocol's per-cell cap (also the client decode
				// scratch size and the compression-scratch sizing basis). Alert in debug, then drop the payload rather
				// than overflow the scratch. The frame is still buffered (ring contiguity) with its sharedCrc, so the
				// client CRC-mismatches and resyncs instead of applying a truncated batch.
				DEBUG_BREAK();
				LOG(kNetwork, kError, "Server::BufferFrame status change count {} exceeds cap {}, dropping payload Coord: ({},{}) Frame: {}", iStatusChangeCount, kiMaxStatusChangesPerCell, rCoord.x, rCoord.y, iTick);
			}
			else
			{
				int64_t iCompressedSize = CompressStatusChangeBatch(rUpdateData.statusChanges.data(), iStatusChangeCount, mCompressionBuffer.data(), static_cast<int64_t>(mCompressionBuffer.size()));
				if (iCompressedSize > 0)
				{
					buffered.compressedData.assign(mCompressionBuffer.begin(), mCompressionBuffer.begin() + iCompressedSize);
				}
				else
				{
					// Compression failed despite the sized scratch — drop the payload (logged kError by the codec)
					// rather than buffer an empty prefix the client would decode as zero changes and silently desync.
					LOG(kNetwork, kError, "Server::BufferFrame compression failed, dropping payload Coord: ({},{}) Frame: {} Count: {}", rCoord.x, rCoord.y, iTick, iStatusChangeCount);
				}
			}
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

	// Evict oldest entries first, recycling their per-coord string storage into the pool so the build
	// loop below serializes into reused capacity (no large allocation once at steady state).
	// Heap: ring/pool grow until steady state
	while (static_cast<int64_t>(mBufferedFullFrames.size()) >= kiMaxBufferedFrames)
	{
		for (auto& [rCoord, rSerialized] : mBufferedFullFrames.front().serializedFrames)
		{
			mFullFramePool.push_back(std::move(rSerialized));
		}
		mBufferedFullFrames.pop_front();
	}

	BufferedFullFrame buffered {};
	buffered.iTick = iTick;

	for (const std::pair<GridCoord, const game::Frame*>& rFrame : frames)
	{
		std::string serialized;
		if (!mFullFramePool.empty())
		{
			serialized = std::move(mFullFramePool.back());
			mFullFramePool.pop_back();
		}
		serialized.clear();

		mFrameStreamBuf.mpTarget = &serialized;
		mFrameStream << *rFrame.second;

		buffered.serializedFrames.insert_or_assign(rFrame.first, std::move(serialized));
	}

	mBufferedFullFrames.push_back(std::move(buffered));
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
