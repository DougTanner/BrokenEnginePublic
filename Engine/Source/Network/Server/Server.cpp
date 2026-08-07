#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/Server/Server.h"
#include "Network/Server/ServerSessionRuntime.h"

#include "Game.h"

namespace engine
{

// NetworkProtocol.h cannot include NetworkManager.h; tie the codec's maximum ack message to the real
// slot ceiling here so a transport slot change trips this assertion.
static_assert(NetworkMessages::ClientAckStreamMessage::kiMaxSlotCount == NetworkManager::kiMaxEnetCoordSlots,
	"ack-stream codec cardinality must match the transport slot ceiling");
static_assert(kiMaxAckStreamPacketSize == NetworkMessages::ClientAckStreamMessage::GetSize(NetworkManager::kiMaxEnetCoordSlots),
	"kiMaxAckStreamPacketSize must match the ack-stream wire layout sized by kiMaxEnetCoordSlots");

Server::Server(uint16_t uiPort, ServerSessionRuntime& rSessionRuntime)
:	mrSessionRuntime(rSessionRuntime)
{
	ASSERT(gpServer == nullptr);

	gpServer = this;

	ENetAddress address {};
	address.host = (gLaunchOptions.flags & LaunchOptionFlags::kLoopbackOnly) ? htonl(INADDR_LOOPBACK) : ENET_HOST_ANY;
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

void Server::Poll(const NetworkTimeState& rTimeState, ServerPollMode ePollMode)
{
	ASSERT(common::gpMultithreading->IsMainThread());

	if (mpHost == nullptr)
	{
		return;
	}

	mPendingDisconnects.clear();
	// mPendingNewSubscriptions / mPendingResyncClientIds are intentionally NOT cleared here. Their consumers
	// (game ServerSession::SendNewSubscriptionFullStates / game ServerSession::HandleResyncRequests) run post-tick,
	// so a per-poll clear would drop a subscribe/resync accepted between servicings. They persist until those
	// consumers service and clear them. While paused (iFullTicks == 0), ServerSessionRuntime::CompleteUpdate
	// services them each update instead, so a client can join a paused server.
	mReceivedGamePackets.clear();

	// Reset the per-update (~ per-tick window) contract budgets before draining this update's packets. The
	// tick-boundary poll skips the reset so both polls of an update share one admission budget window.
	if (ePollMode == ServerPollMode::kUpdateStart)
	{
		for (ClientConnection& rClient : mClients)
		{
			rClient.iTickPacketCount = 0;
			rClient.iTickByteCount = 0;
			std::memset(rClient.tickTypeCounts, 0, sizeof(rClient.tickTypeCounts));
		}
	}

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
				DispatchIncoming(event, rTimeState.bFastForward);
				break;
			case ENET_EVENT_TYPE_NONE:
				break;
		}
	}

	// Process delayed packets whose release time has passed (or flush all when bypassing simulation)
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		NetworkSimulation::ProcessOrFlush(mDelayedPackets, rTimeState.bFastForward, [this](const DelayedPacket& rPacket)
		{
			Receive(rPacket.data, rPacket.pPeer);
		});
	}
}

void Server::DispatchIncoming(ENetEvent& rEvent, bool bFastForward)
{
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
		NetworkSimulation::DispatchOrEnqueue(mDelayedPackets, mNetworkSimState, kSimConfig, bFastForward, rEvent, [this](ENetEvent& rInner)
		{
			Receive(rInner);
		});
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
	connection.slots.resize(NetworkManager::kiMaxEnetCoordSlots);

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
	Receive(std::span<const uint8_t>(rEvent.packet->data, rEvent.packet->dataLength), rEvent.peer);
}

void Server::Receive(std::span<const uint8_t> packetData, ENetPeer* pPeer)
{
	if (packetData.size() < 1)
	{
		return;
	}

	int64_t iClientId = reinterpret_cast<int64_t>(pPeer->data);
	PacketType eType = static_cast<PacketType>(packetData[0]);

	// Gate 1: unknown client id (e.g. packets still in flight after a violation-disconnect + RemoveClient) -> silent drop.
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// Gate 2: per-update (~ per-tick) global packet/byte budget -- applies to every type including game-range.
	// The budget window spans both of an update's polls (Server::Poll resets it only at kUpdateStart).
	// Record a violation only on the FIRST crossing of each budget within the update window; all further
	// over-budget packets that update drop silently. A sustained hostile flood still escalates (~1 violation per
	// update -> disconnect within ~32 updates ~= 1 s at 32 Hz), while a one-off multi-second stall burst (>=288
	// queued acks after a ~9 s server stall, or a NetworkSimulation fast-forward flush draining the delayed
	// queue) costs a legitimate client at most 2 lifetime violations (packet + byte). RecordContractViolation may
	// invalidate pClient, so the first-crossing record is the last touch of the client and returns immediately.
	const bool bPacketWasUnderBudget = pClient->iTickPacketCount <= kiMaxClientPacketsPerTick;
	++pClient->iTickPacketCount;
	if (pClient->iTickPacketCount > kiMaxClientPacketsPerTick)
	{
		if (bPacketWasUnderBudget)
		{
			RecordContractViolation(iClientId, "tick budget", packetData[0], static_cast<int64_t>(packetData.size()));
		}
		return;
	}

	const bool bByteWasUnderBudget = pClient->iTickByteCount <= kiMaxClientInboundBytesPerTick;
	pClient->iTickByteCount += static_cast<int64_t>(packetData.size());
	if (pClient->iTickByteCount > kiMaxClientInboundBytesPerTick)
	{
		if (bByteWasUnderBudget)
		{
			RecordContractViolation(iClientId, "tick budget", packetData[0], static_cast<int64_t>(packetData.size()));
		}
		return;
	}

	// Gates 3-5 apply to engine types only. Game-range types (>= kGamePacketStart) skip the contract table and
	// keep their existing dispatch path (default branch: FindHandshakenClient gate + parse-time contract checks).
	if (static_cast<uint8_t>(eType) < static_cast<uint8_t>(PacketType::kGamePacketStart))
	{
		const ClientPacketContract contract = GetClientPacketContract(eType);

		// Gate 3: contract lookup -- sentinel row (not client-sendable)
		// or size outside [min, max].
		if (contract.iMaxSize == 0)
		{
			RecordContractViolation(iClientId, "not client-sendable", packetData[0], static_cast<int64_t>(packetData.size()));
			return;
		}
		if (static_cast<int64_t>(packetData.size()) < contract.iMinSize || static_cast<int64_t>(packetData.size()) > contract.iMaxSize)
		{
			RecordContractViolation(iClientId, "size out of range", packetData[0], static_cast<int64_t>(packetData.size()));
			return;
		}

		// Gate 4: handshake gate -- silent drop, no violation (pre-Hello ack race: ack streams legitimately
		// arrive before the handshake completes).
		if (contract.bRequiresHandshake && !pClient->bHandshakeComplete)
		{
			return;
		}

		// Gate 5: per-type per-tick cap -- drop; violation only if the contract counts over-cap.
		if (++pClient->tickTypeCounts[packetData[0]] > contract.iMaxPerTick)
		{
			if (contract.bOverCapCountsViolation)
			{
				RecordContractViolation(iClientId, "per-type cap", packetData[0], static_cast<int64_t>(packetData.size()));
			}
			return;
		}
	}

	try
	{
		switch (eType)
		{
			case PacketType::kClientAckStream:
				ClientAckStream(packetData, iClientId);
				break;
			case PacketType::kClientDesyncReport:
				ClientDesyncReport(packetData, iClientId);
				break;
			case PacketType::kClientDebugFrameRequest:
				ClientDebugFrameRequest(packetData, pPeer, iClientId);
				break;
			case PacketType::kClientHello:
				ClientHello(packetData, pPeer, iClientId);
				break;
			case PacketType::kClientSubscribe:
				ClientSubscribe(packetData, iClientId);
				break;
			case PacketType::kClientUnsubscribe:
				ClientUnsubscribe(packetData, iClientId);
				break;
			case PacketType::kClientResyncRequest:
				ClientResyncRequest(packetData, iClientId);
				break;
			default:
				// Only game-range types reach the default -- engine sentinel types are caught at gate 3.
				// Game-range handshake gate stays here (silent drop pre-handshake).
				{
					ClientConnection* pGameClient = FindHandshakenClient(iClientId);
					if (pGameClient == nullptr)
					{
						break;
					}
					ScopedSuppressAllocationTracking suppress;
					// Heap: raw game packet buffer grows on game-specific packets
					mReceivedGamePackets.push_back({iClientId, packetData[0], std::vector<uint8_t>(packetData.begin() + 1, packetData.end())});
				}
				break;
		}
	}
	catch (const std::exception& rException)
	{
		// Trust boundary: a corrupt count/size in a received payload throws CorruptStreamException
		// (or .at()/bad_alloc) from the reader before any client state is mutated (handlers land
		// parsed values in locals first). Drop the single packet and let the client resend/reconnect,
		// rather than tearing down the peer, and count it as a contract violation.
		LOG(kNetwork, kDebug, "Server::Receive dropped corrupt packet (type {}) Client: {}: {}", static_cast<uint8_t>(eType), iClientId, rException.what());
		RecordContractViolation(iClientId, "corrupt payload", packetData[0], static_cast<int64_t>(packetData.size()));
	}
}

void Server::BufferFrame(int64_t iTick, const std::pair<GridCoord, GridUpdateData>* pGridUpdates, int64_t iGridUpdateCount)
{
	miLatestBufferedTick = iTick;
	ScopedSuppressAllocationTracking suppress;

	std::unordered_set<GridCoord> activeCoords;
	activeCoords.reserve(static_cast<size_t>(iGridUpdateCount));

	for (int64_t i = 0; i < iGridUpdateCount; ++i)
	{
		const std::pair<GridCoord, GridUpdateData>& rGridUpdate = pGridUpdates[i];
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
				int64_t iCompressedSize = game::NetworkSessionContract::CompressStatusChanges(rUpdateData.statusChanges.data(), iStatusChangeCount, mCompressionBuffer.data(), static_cast<int64_t>(mCompressionBuffer.size()));
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

void Server::BufferFullFrame(int64_t iTick, const std::pair<GridCoord, const game::Frame*>* pFrames, int64_t iFrameCount)
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

	for (int64_t i = 0; i < iFrameCount; ++i)
	{
		const std::pair<GridCoord, const game::Frame*>& rFrame = pFrames[i];
		std::string serialized;
		if (!mFullFramePool.empty())
		{
			serialized = std::move(mFullFramePool.back());
			mFullFramePool.pop_back();
		}
		serialized.clear();

		mFrameStreamBuf.mpTarget = &serialized;
		game::NetworkSessionContract::WriteFrame(mFrameStream, *rFrame.second);

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

void Server::RecordContractViolation(int64_t iClientId, const char* pcReason, uint8_t uiPacketType, int64_t iSize)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	++pClient->iContractViolations;

	// Log only on the first violation and at disconnect -- exactly two kWarning lines per hostile client,
	// no per-packet spam, no cooldown state.
	if (pClient->iContractViolations == 1)
	{
		LOG(kNetwork, kWarning, "Server::RecordContractViolation First Client: {} Reason: {} Type: {} Size: {}", iClientId, pcReason, uiPacketType, iSize);
	}

	if (pClient->iContractViolations >= kiContractViolationDisconnectCount)
	{
		LOG(kNetwork, kWarning, "Server::RecordContractViolation Disconnecting Client: {} Violations: {} Reason: {} Type: {} Size: {}", iClientId, pClient->iContractViolations, pcReason, uiPacketType, iSize);

		// Capture peer/GUID before the client record is removed below.
		ENetPeer* pPeer = pClient->pPeer;
		ClientGuid clientGuid = pClient->clientGuid;

		{
			ScopedSuppressAllocationTracking suppress;
			// Heap: push the disconnect ourselves (not via the ENet DISCONNECT event) so the game layer
			// persists fleet state; the later DISCONNECT event finds no client (gate 1) and is a safe no-op.
			mPendingDisconnects.push_back({iClientId, clientGuid});
		}

		enet_peer_disconnect(pPeer, 0);
		RemoveClient(iClientId);
	}
}

} // namespace engine

#endif // BT_SERVER
