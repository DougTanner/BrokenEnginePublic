#include "Pch.h"

#include "Network/NetworkServer.h"

#include "Input/Input.h"
#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

static void WritePlayerInputs(common::Workbuffer& rWorkbuffer, std::span<const game::PlayerInput> playerInputs)
{
	int32_t iCount = static_cast<int32_t>(playerInputs.size());
	rWorkbuffer.PushBack<int32_t>(iCount);
	for (int32_t i = 0; i < iCount; ++i)
	{
		const game::PlayerInput& rInput = playerInputs[i];
		uint64_t uiFlags = 0;
		std::memcpy(&uiFlags, &rInput.flags, sizeof(uint64_t));
		rWorkbuffer.PushBack<uint64_t>(uiFlags);
		rWorkbuffer.PushBack<float>(rInput.f3Move.x);
		rWorkbuffer.PushBack<float>(rInput.f3Move.y);
		rWorkbuffer.PushBack<float>(rInput.f3Move.z);
		XMFLOAT4A f4Direction {};
		XMStoreFloat4A(&f4Direction, rInput.vecDirection);
		rWorkbuffer.PushBack<XMFLOAT4A>(f4Direction);
	}
}

NetworkServer::NetworkServer(uint16_t uiPort)
{
	gpNetworkServer = this;

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

	mPendingInputs.clear();
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
				if constexpr (kbEnableNetworkSimulation)
				{
					bool bUnreliable = NetworkManager::IsUnreliableChannel(event.channelID);
					if (bUnreliable)
					{
						if (NetworkSimulation::ShouldDrop())
						{
							FILE_LOG(0, "[NetworkServer] SimDrop: size={}", event.packet->dataLength);
							enet_packet_destroy(event.packet);
							break;
						}
						ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
						// Heap: delay queue copies packet data for deferred processing
						DelayedPacket delayed {};
						delayed.releaseTime = std::chrono::steady_clock::now() + NetworkSimulation::RandomOneWayDelay();
						FILE_LOG(0, "[NetworkServer] SimDelay: size={} delayMs={}", event.packet->dataLength, std::chrono::duration_cast<std::chrono::milliseconds>(delayed.releaseTime - std::chrono::steady_clock::now()).count());
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
	if constexpr (kbEnableNetworkSimulation)
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

	if constexpr (kbEnableNetworkSimulation)
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
		case PacketType::kClientInputStream:
			HandleClientInputStream(pData, iClientId);
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

void NetworkServer::HandleClientInputStream(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	// Read sequence number
	uint32_t uiSequence = ReadUint32(pCursor);

	// Read held flags
	uint64_t uiHeldFlags = ReadUint64(pCursor);
	game::FrameInputHeldFlags_t heldFlags;
	std::memcpy(&heldFlags, &uiHeldFlags, sizeof(uint64_t));

	// Read movement
	XMFLOAT3 f3Move {};
	ReadBytes(pCursor, &f3Move, sizeof(XMFLOAT3));

	// Read direction
	XMVECTOR vecDirection = ReadVec4(pCursor);

	// Derive pressed flags from held state delta (only from newer packets)
	ClientConnection* pClient = FindClient(iClientId);
	game::FrameInputPressedFlags_t pressedFlags {};
	if (pClient != nullptr && static_cast<int32_t>(uiSequence - pClient->uiLatestInputSequence) > 0)
	{
		pClient->uiLatestInputSequence = uiSequence;

		uint64_t uiPrevHeld = 0;
		std::memcpy(&uiPrevHeld, &pClient->previousHeldFlags, sizeof(uint64_t));
		uint64_t uiCurrHeld = 0;
		std::memcpy(&uiCurrHeld, &heldFlags, sizeof(uint64_t));

		// Rising edge: not held before, held now
		uint64_t uiRising = uiCurrHeld & ~uiPrevHeld;

		uint32_t uiPressed = 0;
		// kPrimary (0x0001) -> kTogglePrimary (0x0001)
		if (uiRising & 0x0001)
		{
			uiPressed |= 0x0001;
		}
		// kSecondary (0x0002) -> kToggleSecondary (0x0002)
		if (uiRising & 0x0002)
		{
			uiPressed |= 0x0002;
		}

		std::memcpy(&pressedFlags, &uiPressed, sizeof(uint32_t));
		pClient->previousHeldFlags = heldFlags;
	}

	// Read per-slot ACK state
	uint8_t uiAckSlotCount = ReadUint8(pCursor);
	if (pClient != nullptr)
	{
		for (uint8_t a = 0; a < uiAckSlotCount; ++a)
		{
			uint8_t uiSlotIndex = ReadUint8(pCursor);
			uint16_t uiSlotEpoch = ReadUint16(pCursor);
			int64_t iSlotAckFloor = ReadInt64(pCursor);
			uint64_t uiSlotBitfield = ReadUint64(pCursor);

			if (uiSlotIndex < NetworkManager::kiMaxCoordSlots
			&& pClient->coordSubscriptions[uiSlotIndex].bActive
			&& uiSlotEpoch == pClient->coordAckStates[uiSlotIndex].uiEpoch
			&& iSlotAckFloor >= pClient->coordAckStates[uiSlotIndex].iAckFloor)
			{
				FILE_LOG(0, "[NetworkServer] AckUpdate: client={} slot={} oldFloor={} newFloor={} bitfield={:#x}", pClient->iClientId, uiSlotIndex, pClient->coordAckStates[uiSlotIndex].iAckFloor, iSlotAckFloor, uiSlotBitfield);
				if (iSlotAckFloor == pClient->coordAckStates[uiSlotIndex].iAckFloor)
				{
					pClient->coordAckStates[uiSlotIndex].uiReceivedBitfield |= uiSlotBitfield;
				}
				else
				{
					pClient->coordAckStates[uiSlotIndex].iAckFloor = iSlotAckFloor;
					pClient->coordAckStates[uiSlotIndex].uiReceivedBitfield = uiSlotBitfield;
				}
			}
		}
	}
	else
	{
		// Skip ACK data if client not found
		for (uint8_t a = 0; a < uiAckSlotCount; ++a)
		{
			pCursor += 1 + 2 + 8 + 8; // slotIndex + epoch + ackFloor + bitfield
		}
	}

	// Pipeline RTT: store client timestamp for echo in SendUpdate (monotonically increasing to guard against out-of-order packets)
	int64_t iClientTimestampNs = ReadInt64(pCursor);
	if (pClient != nullptr && iClientTimestampNs > pClient->iClientTimestampNs)
	{
		pClient->iClientTimestampNs = iClientTimestampNs;
	}

	// Deduplicate: if we already have input from this client this tick, OR-merge pressedFlags and overwrite the rest
	PendingInput* pExisting = nullptr;
	for (PendingInput& rPending : mPendingInputs)
	{
		if (rPending.iClientId == iClientId)
		{
			pExisting = &rPending;
			break;
		}
	}

	if (pExisting != nullptr)
	{
		// Always OR-merge presses regardless of order
		uint32_t uiExistingPressed = 0;
		std::memcpy(&uiExistingPressed, &pExisting->pressedFlags, sizeof(uint32_t));
		uint32_t uiNewPressed = 0;
		std::memcpy(&uiNewPressed, &pressedFlags, sizeof(uint32_t));
		uiExistingPressed |= uiNewPressed;
		std::memcpy(&pExisting->pressedFlags, &uiExistingPressed, sizeof(uint32_t));

		// Only update held/move/direction from newer packets
		if (static_cast<int32_t>(uiSequence - pExisting->uiInputSequence) > 0)
		{
			pExisting->heldFlags = heldFlags;
			pExisting->f3Move = f3Move;
			pExisting->vecDirection = vecDirection;
			pExisting->uiInputSequence = uiSequence;
		}
	}
	else
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: pending input vector grows each tick
		mPendingInputs.push_back({
			.iClientId = iClientId,
			.heldFlags = heldFlags,
			.f3Move = f3Move,
			.vecDirection = vecDirection,
			.pressedFlags = pressedFlags,
			.uiInputSequence = uiSequence,
		});
	}
}

void NetworkServer::HandleClientSpawnRequest(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiFlags = ReadUint8(pCursor);

	ClientRequestFlags_t flags;
	std::memcpy(&flags, &uiFlags, sizeof(uint8_t));

	common::Log("NetworkServer: Client {} spawn request (spawn={}, respawn={})", iClientId, static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: spawn request vector grows on request
	mPendingSpawnRequests.push_back({iClientId, flags});
}

void NetworkServer::HandleClientDesyncReport(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	uint64_t uiExpectedCrc = ReadUint64(pCursor);
	uint64_t uiActualCrc = ReadUint64(pCursor);

	char pcExpected[20] {};
	char pcActual[20] {};
	common::Log("NetworkServer: Desync report frame {} grid ({},{}) expected={} actual={}", iFrame, coord.x, coord.y, common::ToHex(std::span(pcExpected), uiExpectedCrc), common::ToHex(std::span(pcActual), uiActualCrc));
}

void NetworkServer::SendAssignPlayer(int64_t iClientId, game::player_t playerId, GridCoord coord)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	pClient->humanPlayerId = playerId;
	pClient->humanGridCoord = coord;

	common::Log("NetworkServer: Sending assign player to client {} (player={}, grid ({},{}))", iClientId, playerId.ToUuid().Value(), coord.x, coord.y);
	FILE_LOG(0, "[SendAssignPlayer] client={} player={} grid=({},{})", iClientId, playerId.ToUuid().Value(), coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B player_t ID][GridCoord]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerAssignPlayer));
	rWorkbuffer.PushBack<int64_t>(playerId.ToUuid().Value());
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pClient->pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iFrame, GridCoord coord, const game::Frame* pFrame)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	common::Log("NetworkServer: Sending coord full state to client {} (frame {}, slot {}, coord ({},{}))", iClientId, iFrame, iSlot, coord.x, coord.y);
	FILE_LOG(0, "[SendCoordFullState] client={} frame={} slot={} coord=({},{})", iClientId, iFrame, iSlot, coord.x, coord.y);

	// Serialize frame to a temporary stringstream
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: stringstream allocates for frame serialization
	std::ostringstream frameStream(std::ios::binary);
	frameStream << *pFrame;
	std::string frameData = frameStream.str();

	// LZ4 compress
	int iCompressedSize = CompressToBuffer(frameData.data(), static_cast<int>(frameData.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B slotIndex][2B epoch][8B frame][4B coord.x][4B coord.y][4B uncompressedSize][4B compressedSize][...LZ4 data]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerCoordFullState));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(pClient->coordAckStates[iSlot].uiEpoch);
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(frameData.size()));
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
	rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(mCompressionBuffer.data()), iCompressedSize));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pClient->pPeer, NetworkManager::CoordSlotReliable(iSlot), pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::BufferFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, GridUpdateData>>& rGridUpdates)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	for (const auto& [coord, updateData] : rGridUpdates)
	{
		// Heap: per-coord ring buffer grows until steady state
		PerCoordBufferedFrame buffered {};
		buffered.iFrame = iFrame;
		buffered.serverCrc = updateData.serverCrc;

		if (!updateData.statusChanges.empty())
		{
			int64_t iCompressedSize = CompressStatusChangeBatch(updateData.statusChanges.data(), static_cast<int64_t>(updateData.statusChanges.size()), mCompressionBuffer.data(), kiMaxPacketSize);
			buffered.compressedData.assign(mCompressionBuffer.begin(), mCompressionBuffer.begin() + iCompressedSize);
		}

		buffered.playerInputs.assign(updateData.playerInputs.begin(), updateData.playerInputs.end());

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

void NetworkServer::BufferFullFrame(int64_t iFrame, const std::vector<std::pair<GridCoord, const game::Frame*>>& rFrames)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Heap: ring buffer grows until steady state
	BufferedFullFrame buffered {};
	buffered.iFrame = iFrame;

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

void NetworkServer::HandleClientDebugFrameRequest(const uint8_t* pData, ENetPeer* pPeer)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	common::Log("NetworkServer: Debug frame request frame {} grid ({},{})", iFrame, coord.x, coord.y);

	// Find the frame in the ring buffer
	const BufferedFullFrame* pBuffered = nullptr;
	for (const BufferedFullFrame& rBuffered : mBufferedFullFrames)
	{
		if (rBuffered.iFrame == iFrame)
		{
			pBuffered = &rBuffered;
			break;
		}
	}

	if (pBuffered == nullptr)
	{
		common::Log("NetworkServer: Debug frame {} not found in buffer", iFrame);
		return;
	}

	auto it = pBuffered->serializedFrames.find(coord);
	if (it == pBuffered->serializedFrames.end())
	{
		common::Log("NetworkServer: Debug frame {} coord ({},{}) not found", iFrame, coord.x, coord.y);
		return;
	}

	const std::string& rFrameData = it->second;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// LZ4 compress (reuse persistent compression buffer)
	int iCompressedSize = CompressToBuffer(rFrameData.data(), static_cast<int>(rFrameData.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B frame][4B gridX][4B gridY][4B uncompressedSize][4B compressedSize][...LZ4 data]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerDebugFrame));
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(rFrameData.size()));
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
	rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(mCompressionBuffer.data()), iCompressedSize));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs)
{
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(uiEpoch);
	rWorkbuffer.PushBack<int64_t>(rBuffered.iFrame);
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);
	rWorkbuffer.PushBack<uint64_t>(rBuffered.serverCrc);

	int32_t iCompressedSize = static_cast<int32_t>(rBuffered.compressedData.size());
	rWorkbuffer.PushBack<int32_t>(iCompressedSize);
	if (iCompressedSize > 0)
	{
		rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rBuffered.compressedData.data()), iCompressedSize));
	}

	WritePlayerInputs(rWorkbuffer, rBuffered.playerInputs);
}

void NetworkServer::SendUpdate(ClientConnection& rClient, int64_t iFrame)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Send one packet per active subscription slot
	for (int64_t iSlot = 0; iSlot < NetworkManager::kiMaxCoordSlots; ++iSlot)
	{
		if (!rClient.coordSubscriptions[iSlot].bActive)
		{
			continue;
		}

		GridCoord coord = rClient.coordSubscriptions[iSlot].coord;

		const PerCoordBufferedFrame* pBuffered = FindBufferedFrame(coord, iFrame);
		if (pBuffered == nullptr)
		{
			continue;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		rWorkbuffer.Push();

		WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordUpdate, iSlot, rClient.coordAckStates[iSlot].uiEpoch, *pBuffered, rClient.iClientTimestampNs);

		std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();
		FILE_LOG(0, "[NetworkServer] SendUpdate: frame={} client={} slot={} coord=({},{}) size={}", iFrame, rClient.iClientId, iSlot, coord.x, coord.y, packetSpan.size());

		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
		enet_peer_send(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), pPacket);

		rWorkbuffer.Pop();
	}
}

void NetworkServer::SendResends(ClientConnection& rClient, int64_t iFrame)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	int64_t iTotalResendCount = 0;

	// Iterate per-slot: each active subscription has its own ACK state and coord ring buffer
	for (int64_t iSlot = 0; iSlot < NetworkManager::kiMaxCoordSlots; ++iSlot)
	{
		if (!rClient.coordSubscriptions[iSlot].bActive)
		{
			continue;
		}

		const PerCoordAckState& rAck = rClient.coordAckStates[iSlot];
		if (rAck.iAckFloor < 0)
		{
			continue;
		}

		GridCoord coord = rClient.coordSubscriptions[iSlot].coord;

		if (rAck.uiReceivedBitfield == 0)
		{
			continue;
		}
		int64_t iScanLimit = static_cast<int64_t>(std::bit_width(rAck.uiReceivedBitfield));

		int64_t iSlotResendCount = 0;
		for (int64_t iBit = 0; iBit < iScanLimit && iSlotResendCount < kiMaxResendFrames; ++iBit)
		{
			if (rAck.uiReceivedBitfield & (1ULL << iBit))
			{
				continue;
			}

			int64_t iMissingFrame = rAck.iAckFloor + 1 + iBit;
			if (iMissingFrame >= iFrame)
			{
				break;
			}

			const PerCoordBufferedFrame* pBuffered = FindBufferedFrame(coord, iMissingFrame);
			if (pBuffered == nullptr)
			{
				continue;
			}

			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			rWorkbuffer.Push();

			WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordResend, iSlot, rClient.coordAckStates[iSlot].uiEpoch, *pBuffered, rClient.iClientTimestampNs);

			std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();
			FILE_LOG(0, "[NetworkServer] Resend: frame={} to client={} slot={} coord=({},{}) ackFloor={} bitfield={:#x}", iMissingFrame, rClient.iClientId, iSlot, coord.x, coord.y, rAck.iAckFloor, rAck.uiReceivedBitfield);

			// Heap: ENet allocates packet data internally
			ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
			enet_peer_send(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), pPacket);

			rWorkbuffer.Pop();

			++iSlotResendCount;
			++iTotalResendCount;
		}
	}

	FILE_LOG(0, "[NetworkServer] ResendSummary: client={} totalSent={} currentFrame={}", rClient.iClientId, iTotalResendCount, iFrame);
}

void NetworkServer::SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerConnectionResponse));
	rWorkbuffer.PushBack<uint8_t>(bAccepted ? 1 : 0);
	if (!bAccepted && pMessage != nullptr)
	{
		rWorkbuffer.Append(std::string_view(pMessage));
	}

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::HandleClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId)
{
	char pcClientConfig[64] = {};
	size_t iLength = std::min(iSize - 1, sizeof(pcClientConfig) - 1);
	std::memcpy(pcClientConfig, pData + 1, iLength);

	if (strcmp(pcClientConfig, kpcBuildConfigName) != 0)
	{
		char pcMessage[256] {};
		snprintf(pcMessage, sizeof(pcMessage), "Build mismatch: server is %s, client is %s", kpcBuildConfigName, pcClientConfig);
		common::Log("NetworkServer: Rejecting client {} ({})", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage);

		// Remove from mClients (added during HandleConnect before hello arrived)
		RemoveClient(iClientId);

		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	common::Log("NetworkServer: Client {} hello accepted (config: {})", iClientId, pcClientConfig);
	SendConnectionResponse(pPeer, true, nullptr);
}

void NetworkServer::HandleClientSubscribe(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	GridCoord coord = ReadGridCoord(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// Already subscribed?
	if (pClient->IsCoordSubscribed(coord))
	{
		return;
	}

	int64_t iSlot = pClient->AllocateSlot();
	if (iSlot < 0)
	{
		common::Log("NetworkServer: No free coord slot for client {} subscribing to ({},{})", iClientId, coord.x, coord.y);
		SendSubscribeAccept(*pClient, 0xFF, coord);
		return;
	}

	pClient->coordSubscriptions[iSlot].coord = coord;
	pClient->coordSubscriptions[iSlot].bActive = true;
	++pClient->coordAckStates[iSlot].uiEpoch;

	common::Log("NetworkServer: Client {} subscribed to ({},{}) slot {}", iClientId, coord.x, coord.y, iSlot);
	FILE_LOG(0, "[HandleClientSubscribe] client={} coord=({},{}) slot={}", iClientId, coord.x, coord.y, iSlot);

	SendSubscribeAccept(*pClient, iSlot, coord);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: pending subscription entry
	mPendingNewSubscriptions.push_back({iClientId, iSlot, coord});
}

void NetworkServer::HandleClientUnsubscribe(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	if (uiSlotIndex >= NetworkManager::kiMaxCoordSlots || !pClient->coordSubscriptions[uiSlotIndex].bActive)
	{
		return;
	}

	GridCoord coord = pClient->coordSubscriptions[uiSlotIndex].coord;
	pClient->FreeSlot(uiSlotIndex);

	common::Log("NetworkServer: Client {} unsubscribed slot {} coord ({},{})", iClientId, uiSlotIndex, coord.x, coord.y);
	FILE_LOG(0, "[HandleClientUnsubscribe] client={} slot={} coord=({},{})", iClientId, uiSlotIndex, coord.x, coord.y);

	SendUnsubscribeAck(*pClient, uiSlotIndex);
}

void NetworkServer::SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B slotIndex][2B epoch][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerSubscribeAccept));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	uint16_t uiEpoch = (iSlot < NetworkManager::kiMaxCoordSlots) ? rClient.coordAckStates[iSlot].uiEpoch : 0;
	rWorkbuffer.PushBack<uint16_t>(uiEpoch);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(rClient.pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::SendUnsubscribeAck(ClientConnection& rClient, int64_t iSlot)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B slotIndex]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerUnsubscribeAck));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(rClient.pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

const PerCoordBufferedFrame* NetworkServer::FindBufferedFrame(GridCoord coord, int64_t iFrame) const
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
	int64_t iIndex = iFrame - rCoordBuffer.front().iFrame;
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
