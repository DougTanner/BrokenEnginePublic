#include "Pch.h"

#include "Network/NetworkClient.h"

#include "Input/Input.h"
#include "Memory/MemoryManager.h"

namespace engine
{

// Cursor read helpers
static void ReadBytes(const uint8_t*& pCursor, void* pDest, int64_t iSize)
{
	std::memcpy(pDest, pCursor, iSize);
	pCursor += iSize;
}

static int16_t ReadInt16(const uint8_t*& pCursor)
{
	int16_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int16_t));
	return i;
}

static int32_t ReadInt32(const uint8_t*& pCursor)
{
	int32_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int32_t));
	return i;
}

static int64_t ReadInt64(const uint8_t*& pCursor)
{
	int64_t i = 0;
	ReadBytes(pCursor, &i, sizeof(int64_t));
	return i;
}

static GridCoord ReadGridCoord(const uint8_t*& pCursor)
{
	GridCoord coord {};
	coord.x = ReadInt32(pCursor);
	coord.y = ReadInt32(pCursor);
	return coord;
}

static void ReadPlayerInputs(const uint8_t*& pCursor, std::vector<game::PlayerInput>& rOut)
{
	int32_t iCount = ReadInt32(pCursor);
	rOut.resize(iCount);
	for (int32_t p = 0; p < iCount; ++p)
	{
		uint64_t uiFlags = 0;
		ReadBytes(pCursor, &uiFlags, sizeof(uint64_t));
		std::memcpy(&rOut.at(p).flags, &uiFlags, sizeof(uint64_t));
		ReadBytes(pCursor, &rOut.at(p).f3Move.x, sizeof(float));
		ReadBytes(pCursor, &rOut.at(p).f3Move.y, sizeof(float));
		ReadBytes(pCursor, &rOut.at(p).f3Move.z, sizeof(float));
		XMFLOAT4A f4Direction {};
		ReadBytes(pCursor, &f4Direction, sizeof(XMFLOAT4A));
		rOut.at(p).vecDirection = XMLoadFloat4A(&f4Direction);
	}
}

NetworkClient::NetworkClient(const char* pServerAddress, uint16_t uiPort)
{
	gpNetworkClient = this;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(nullptr, 1, NetworkManager::kuiChannelCount, 0, 0);
	// 1MB send/receive buffers to handle bursty packet traffic
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_SNDBUF, 1024 * 1024);
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_RCVBUF, 1024 * 1024);

	ENetAddress address {};
	enet_address_set_host(&address, pServerAddress);
	address.port = uiPort;

	// Heap: ENet allocates peer data internally
	mpServerPeer = enet_host_connect(mpHost, &address, NetworkManager::kuiChannelCount, 0);

	ENetAddress localAddress {};
	enet_socket_get_address(mpHost->socket, &localAddress);
	char pcServerAddress[64] {};
	enet_address_get_host_ip(&address, pcServerAddress, sizeof(pcServerAddress));
	FILE_LOG(0, "[NetworkClient] Connecting: server={}:{} localPort={}", pcServerAddress, address.port, localAddress.port);
}

NetworkClient::~NetworkClient()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		enet_peer_disconnect(mpServerPeer, 0);

		// Allow time for disconnect to be sent
		ENetEvent event {};
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		while (enet_host_service(mpHost, &event, 100) > 0)
		{
			if (event.type == ENET_EVENT_TYPE_DISCONNECT)
			{
				break;
			}
			if (event.type == ENET_EVENT_TYPE_RECEIVE)
			{
				enet_packet_destroy(event.packet);
			}
		}
	}

	if (mpHost != nullptr)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_host_destroy(mpHost);
	}

	gpNetworkClient = nullptr;
}

void NetworkClient::Poll()
{
	if (mpHost == nullptr)
	{
		return;
	}

	mReceivedUpdates.clear();
	mReceivedFullStates.clear();

	int64_t iReceiveCount = 0;
	ENetEvent event {};
	while (enet_host_service(mpHost, &event, 0) > 0)
	{
		switch (event.type)
		{
		case ENET_EVENT_TYPE_CONNECT:
		{
			mbConnected = true;
			// Disable ENet peer throttle to prevent unreliable packet drops during reconciliation stalls
			enet_peer_throttle_configure(mpServerPeer, UINT32_MAX, 0, 0);
			common::Log("NetworkClient: Connected to server");
			char pcServerAddress[64] {};
			enet_address_get_host_ip(&mpServerPeer->address, pcServerAddress, sizeof(pcServerAddress));
			ENetAddress localAddress {};
			enet_socket_get_address(mpHost->socket, &localAddress);
			FILE_LOG(0, "[NetworkClient] Connected: server={}:{} localPort={}", pcServerAddress, mpServerPeer->address.port, localAddress.port);
			break;
		}
		case ENET_EVENT_TYPE_DISCONNECT:
			mbConnected = false;
			mbDisconnectedEvent = true;
			mpServerPeer = nullptr;
			common::Log("NetworkClient: Disconnected from server");
			FILE_LOG(0, "[NetworkClient] Disconnected from server");
			break;
		case ENET_EVENT_TYPE_RECEIVE:
			++iReceiveCount;
			HandleReceive(event);
			enet_packet_destroy(event.packet);
			break;
		case ENET_EVENT_TYPE_NONE:
			break;
		}
	}

	if (iReceiveCount > 0)
	{
		FILE_LOG(0, "[NetworkClient] Poll: received={} packets ackFloor={}", iReceiveCount, miAckFloor);
	}

	// Track bandwidth deltas from host-level cumulative counters
	uint32_t uiReceivedData = mpHost->totalReceivedData;
	uint32_t uiSentData = mpHost->totalSentData;
	mBytesInPerSecond.Set(static_cast<int64_t>(uiReceivedData - muiPrevReceivedData));
	mBytesOutPerSecond.Set(static_cast<int64_t>(uiSentData - muiPrevSentData));
	muiPrevReceivedData = uiReceivedData;
	muiPrevSentData = uiSentData;
}

void NetworkClient::HandleReceive(ENetEvent& rEvent)
{
	const uint8_t* pData = rEvent.packet->data;
	size_t iSize = rEvent.packet->dataLength;

	if (iSize < 1)
	{
		return;
	}

	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
	case PacketType::kServerAssignPlayer:
		HandleServerAssignPlayer(pData, iSize);
		break;
	case PacketType::kServerFullState:
		HandleServerFullState(pData, iSize);
		break;
	case PacketType::kServerUpdateStream:
		HandleServerUpdateStream(pData, iSize);
		break;
	case PacketType::kServerResendStream:
		HandleServerResendStream(pData, iSize);
		break;
	case PacketType::kServerDebugFrame:
		HandleServerDebugFrame(pData, iSize);
		break;
	default:
		break;
	}
}

void NetworkClient::HandleServerAssignPlayer(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iPlayerIdValue = ReadInt64(pCursor);
	mAssignedPlayerId = game::player_t(uuid_t(iPlayerIdValue));

	mAssignedGridCoord = ReadGridCoord(pCursor);

	common::Log("NetworkClient: Assigned player ID {} at grid ({},{})", iPlayerIdValue, mAssignedGridCoord.x, mAssignedGridCoord.y);
}

void NetworkClient::HandleServerFullState(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	int16_t iCoordCount = ReadInt16(pCursor);

	common::Log("NetworkClient: Received full state frame {} ({} coords)", iFrame, iCoordCount);

	for (int16_t c = 0; c < iCoordCount; ++c)
	{
		GridCoord coord = ReadGridCoord(pCursor);
		int32_t iUncompressedSize = ReadInt32(pCursor);
		int32_t iCompressedSize = ReadInt32(pCursor);

		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

		// Heap: temporary buffer for LZ4 decompression
		std::vector<char> decompressed(iUncompressedSize);
		int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pCursor), decompressed.data(), iCompressedSize, iUncompressedSize);
		pCursor += iCompressedSize;

		if (iDecompressResult < 0)
		{
			common::Log("NetworkClient: LZ4 decompression failed for full state coord ({},{}) frame {} (error={})", coord.x, coord.y, iFrame, iDecompressResult);
			continue;
		}

		// Deserialize frame from decompressed data
		// Heap: stringstream allocates for frame deserialization
		std::string frameStr(decompressed.begin(), decompressed.end());
		std::istringstream frameStream(frameStr, std::ios::binary);

		// Heap: Frame allocation
		auto pFrame = std::make_unique<game::Frame>();
		pFrame->ServerRead(frameStream);

		ReceivedFullState fullState {};
		fullState.iFrame = iFrame;
		fullState.coord = coord;
		fullState.pFrame = std::move(pFrame);

		// Heap: received full states vector grows on new cell data
		mReceivedFullStates.push_back(std::move(fullState));
	}

	// Full state is reliable, so advance ACK floor past this frame
	if (iFrame > miAckFloor)
	{
		miAckFloor = iFrame;
		muiReceivedBitfield = 0;
	}
}

void NetworkClient::HandleServerUpdateStream(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);

	// Pipeline RTT: read echoed client timestamp
	int64_t iEchoedTimestampNs = ReadInt64(pCursor);
	if (iEchoedTimestampNs > 0)
	{
		int64_t iNowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		int64_t iRttUs = (iNowNs - iEchoedTimestampNs) / 1000;
		mSmoothedPipelineRttUs = iRttUs;
		mSmoothedPipelineRttUs.Update();
	}

	// Current frame
	int16_t iGridCount = ReadInt16(pCursor);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedUpdate update {};
	update.iFrame = iFrame;
	// Heap: grid updates vector, variable per frame
	update.gridUpdates.resize(iGridCount);

	for (int16_t g = 0; g < iGridCount; ++g)
	{
		ReceivedGridUpdate& rGridUpdate = update.gridUpdates.at(g);
		rGridUpdate.coord = ReadGridCoord(pCursor);

		rGridUpdate.serverCrc = static_cast<common::crc_t>(ReadInt64(pCursor));

		int32_t iCompSize = ReadInt32(pCursor);

		if (iCompSize > 0)
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			game::StatusChange* pStatusChanges = rWorkbuffer.PushBuffer<game::StatusChange*>(kiMaxStatusChangesPerCell * static_cast<int64_t>(sizeof(game::StatusChange)));
			int64_t iCount = DecompressStatusChangeBatch(pCursor, iCompSize, pStatusChanges, kiMaxStatusChangesPerCell);
			pCursor += iCompSize;

			// Heap: status changes vector per grid cell
			rGridUpdate.statusChanges.assign(pStatusChanges, pStatusChanges + iCount);
			rWorkbuffer.Pop();
		}

		// Heap: player inputs vector per grid cell
		ReadPlayerInputs(pCursor, rGridUpdate.playerInputs);
	}

	// Heap: received updates vector grows each tick
	mReceivedUpdates.push_back(std::move(update));
	TrackReceivedFrame(iFrame);

	// Legacy re-send count (now always 0, re-sends arrive as separate kServerResendStream packets)
	[[maybe_unused]] int16_t iResendCount = ReadInt16(pCursor);

	FILE_LOG(0, "[NetworkClient] UpdateStream: frame={} gridCells={}", iFrame, iGridCount);
}

void NetworkClient::HandleServerResendStream(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iResendFrame = ReadInt64(pCursor);
	int16_t iResendGridCount = ReadInt16(pCursor);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedUpdate resendUpdate {};
	resendUpdate.iFrame = iResendFrame;
	// Heap: grid updates vector for re-sent frame
	resendUpdate.gridUpdates.resize(iResendGridCount);

	for (int16_t g = 0; g < iResendGridCount; ++g)
	{
		ReceivedGridUpdate& rGridUpdate = resendUpdate.gridUpdates.at(g);
		rGridUpdate.coord = ReadGridCoord(pCursor);

		rGridUpdate.serverCrc = static_cast<common::crc_t>(ReadInt64(pCursor));

		int32_t iResendCompSize = ReadInt32(pCursor);

		if (iResendCompSize > 0)
		{
			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			game::StatusChange* pStatusChanges = rWorkbuffer.PushBuffer<game::StatusChange*>(kiMaxStatusChangesPerCell * static_cast<int64_t>(sizeof(game::StatusChange)));
			int64_t iCount = DecompressStatusChangeBatch(pCursor, iResendCompSize, pStatusChanges, kiMaxStatusChangesPerCell);
			pCursor += iResendCompSize;

			rGridUpdate.statusChanges.assign(pStatusChanges, pStatusChanges + iCount);
			rWorkbuffer.Pop();
		}

		// Heap: player inputs vector per grid cell (re-send)
		ReadPlayerInputs(pCursor, rGridUpdate.playerInputs);
	}

	FILE_LOG(0, "[NetworkClient] ResendStream: frame={} gridCells={}", iResendFrame, iResendGridCount);

	// Heap: received updates vector grows for re-sent frames
	mReceivedUpdates.push_back(std::move(resendUpdate));
	TrackReceivedFrame(iResendFrame);
}

void NetworkClient::TrackReceivedFrame(int64_t iFrame)
{
	// First frame received, initialize the ACK floor
	if (miAckFloor < 0)
	{
		miAckFloor = iFrame;
		return;
	}

	// Already acknowledged
	if (iFrame <= miAckFloor)
	{
		return;
	}

	int64_t iBitIndex = iFrame - miAckFloor - 1;

	if (iBitIndex >= kiMaxMissingFrames / 2)
	{
		FILE_LOG(0, "[NetworkClient] WARNING: Frame gap growing: gap={} ackFloor={} receivedFrame={}", iBitIndex + 1, miAckFloor, iFrame);
		DEBUG_BREAK();
	}

	if (iBitIndex >= kiMaxMissingFrames)
	{
		common::Log("NetworkClient: Too many missing frames (gap={}), disconnecting", iBitIndex + 1);
		mbDisconnectedEvent = true;
		return;
	}

	// Mark this frame as received and advance the floor past any contiguous run
	muiReceivedBitfield |= (1ULL << iBitIndex);

	while (muiReceivedBitfield & 1ULL)
	{
		++miAckFloor;
		muiReceivedBitfield >>= 1;
	}

	FILE_LOG(0, "[NetworkClient] AckAdvance: newFloor={} bitfield={:#x}", miAckFloor, muiReceivedBitfield);
}

void NetworkClient::SendInput(uint16_t uiPlayerId, const game::PlayerInput& rInput, bool bGamepad, float fRotateEye)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][2B playerId]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientInputStream));
	rWorkbuffer.PushBack<uint16_t>(uiPlayerId);

	// Held flags
	uint64_t uiHeldFlags = 0;
	std::memcpy(&uiHeldFlags, &rInput.flags, sizeof(uint64_t));
	rWorkbuffer.PushBack<uint64_t>(uiHeldFlags);

	// Movement
	rWorkbuffer.PushBack<XMFLOAT3>(rInput.f3Move);

	// Direction (as XMFLOAT4A)
	XMFLOAT4A f4Direction {};
	XMStoreFloat4A(&f4Direction, rInput.vecDirection);
	rWorkbuffer.PushBack<XMFLOAT4A>(f4Direction);

	// Gamepad + rotateEye
	uint8_t uiGamepad = bGamepad ? 1 : 0;
	rWorkbuffer.PushBack<uint8_t>(uiGamepad);
	rWorkbuffer.PushBack<float>(fRotateEye);

	// ACK state for proactive re-sends
	rWorkbuffer.PushBack<int64_t>(miAckFloor);
	rWorkbuffer.PushBack<uint64_t>(muiReceivedBitfield);

	// Pipeline RTT: embed client timestamp for server to echo back
	int64_t iTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelUnreliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendSpawnRequest(ClientRequestFlags_t flags)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientSpawnRequest));
	uint8_t uiFlags = 0;
	std::memcpy(&uiFlags, &flags, sizeof(uint8_t));
	rWorkbuffer.PushBack<uint8_t>(uiFlags);

	common::Log("NetworkClient: Sending spawn request (spawn={}, respawn={})", static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendDesyncReport(int64_t iFrame, GridCoord coord, common::crc_t expected, common::crc_t actual)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientDesyncReport));
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<uint64_t>(expected);
	rWorkbuffer.PushBack<uint64_t>(actual);

	char pcExpected[20] {};
	char pcActual[20] {};
	common::Log("NetworkClient: Sending desync report frame {} grid ({},{}) expected={} actual={}", iFrame, coord.x, coord.y, common::ToHex(std::span(pcExpected), expected), common::ToHex(std::span(pcActual), actual));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendDebugFrameRequest(int64_t iFrame, GridCoord coord)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B frame][4B gridX][4B gridY]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientDebugFrameRequest));
	rWorkbuffer.PushBack<int64_t>(iFrame);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	common::Log("NetworkClient: Sending debug frame request frame {} grid ({},{})", iFrame, coord.x, coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::HandleServerDebugFrame(const uint8_t* pData, [[maybe_unused]] size_t iSize)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	int32_t iUncompressedSize = ReadInt32(pCursor);
	int32_t iCompressedSize = ReadInt32(pCursor);

	common::Log("NetworkClient: Received debug frame {} grid ({},{}) uncompressed={} compressed={}", iFrame, coord.x, coord.y, iUncompressedSize, iCompressedSize);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Heap: temporary buffer for LZ4 decompression
	std::vector<char> decompressed(iUncompressedSize);
	int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pCursor), decompressed.data(), iCompressedSize, iUncompressedSize);

	if (iDecompressResult < 0)
	{
		common::Log("NetworkClient: LZ4 decompression failed for debug frame {} (error={})", iFrame, iDecompressResult);
		return;
	}

	// Heap: stringstream allocates for frame deserialization
	std::string frameStr(decompressed.begin(), decompressed.end());
	std::istringstream frameStream(frameStr, std::ios::binary);

	// Heap: Frame allocation
	auto pFrame = std::make_unique<game::Frame>();
	pFrame->ServerRead(frameStream);

	mpReceivedDebugFrame = std::make_unique<ReceivedDebugFrame>();
	mpReceivedDebugFrame->iFrame = iFrame;
	mpReceivedDebugFrame->coord = coord;
	mpReceivedDebugFrame->pFrame = std::move(pFrame);
}

void NetworkClient::Flush()
{
	enet_host_flush(mpHost);
}

void NetworkClient::Disconnect()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_peer_disconnect(mpServerPeer, 0);
		mbConnected = false;
	}
}

} // namespace engine
