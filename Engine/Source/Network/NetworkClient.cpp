#include "Pch.h"

#include "Network/NetworkClient.h"

#include "Input/Input.h"
#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

static void ReadPlayerInputs(const uint8_t*& pCursor, std::vector<game::PlayerInput>& rOut)
{
	int32_t iCount = ReadInt32(pCursor);
	rOut.resize(iCount);
	for (int32_t p = 0; p < iCount; ++p)
	{
		uint64_t uiFlags = ReadUint64(pCursor);
		std::memcpy(&rOut.at(p).flags, &uiFlags, sizeof(uint64_t));
		rOut.at(p).f3Move.x = ReadFloat(pCursor);
		rOut.at(p).f3Move.y = ReadFloat(pCursor);
		rOut.at(p).f3Move.z = ReadFloat(pCursor);
		XMFLOAT4A f4Direction {};
		ReadBytes(pCursor, &f4Direction, sizeof(XMFLOAT4A));
		rOut.at(p).vecDirection = XMLoadFloat4A(&f4Direction);
	}
}

static std::unique_ptr<game::Frame> DecompressAndReadFrame(const uint8_t*& pCursor)
{
	int32_t iUncompressedSize = ReadInt32(pCursor);
	int32_t iCompressedSize = ReadInt32(pCursor);

	std::string decompressed(iUncompressedSize, '\0');
	int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pCursor), decompressed.data(), iCompressedSize, iUncompressedSize);
	pCursor += iCompressedSize;

	if (iDecompressResult < 0)
	{
		return nullptr;
	}

	std::istringstream frameStream(std::move(decompressed), std::ios::binary);
	auto pFrame = std::make_unique<game::Frame>();
	pFrame->ServerRead(frameStream);
	return pFrame;
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

	for (std::vector<ReceivedCoordUpdate>& rSlotUpdates : mReceivedCoordUpdates)
	{
		rSlotUpdates.clear();
	}
	mReceivedFullStates.clear();
	mReceivedAssignments.clear();

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
			SendHello();
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
			if constexpr (kbEnableNetworkSimulation)
			{
				bool bUnreliable = NetworkManager::IsUnreliableChannel(event.channelID);
				if (bUnreliable)
				{
					if (NetworkSimulation::ShouldDrop())
					{
						enet_packet_destroy(event.packet);
						break;
					}
					ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
					// Heap: delay queue copies packet data for deferred processing
					DelayedPacket delayed {};
					delayed.releaseTime = std::chrono::steady_clock::now() + NetworkSimulation::RandomOneWayDelay();
					delayed.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
					delayed.uiChannelId = event.channelID;
					auto insertPos = std::lower_bound(mDelayedPackets.begin(), mDelayedPackets.end(), delayed,
					[](const DelayedPacket& rA, const DelayedPacket& rB) { return rA.releaseTime < rB.releaseTime; });
					mDelayedPackets.insert(insertPos, std::move(delayed));
					enet_packet_destroy(event.packet);
				}
				else
				{
					++iReceiveCount;
					HandleReceive(event);
					enet_packet_destroy(event.packet);
				}
			}
			else
			{
				++iReceiveCount;
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
			++iReceiveCount;
			HandleReceive(mDelayedPackets.front().data.data(), mDelayedPackets.front().data.size());
			mDelayedPackets.pop_front();
		}
	}

	if (iReceiveCount > 0)
	{
		FILE_LOG(0, "[NetworkClient] Poll: received={} packets", iReceiveCount);
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
	HandleReceive(rEvent.packet->data, rEvent.packet->dataLength);
}

void NetworkClient::HandleReceive(const uint8_t* pData, size_t iSize)
{
	if (iSize < 1)
	{
		return;
	}

	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
	case PacketType::kServerAssignPlayer:
		HandleServerAssignPlayer(pData);
		break;
	case PacketType::kServerCoordFullState:
		HandleServerCoordFullState(pData);
		break;
	case PacketType::kServerCoordUpdate:
		HandleServerCoordUpdateOrResend(pData, true);
		break;
	case PacketType::kServerCoordResend:
		HandleServerCoordUpdateOrResend(pData, false);
		break;
	case PacketType::kServerDebugFrame:
		HandleServerDebugFrame(pData);
		break;
	case PacketType::kServerConnectionResponse:
		HandleServerConnectionResponse(pData, iSize);
		break;
	case PacketType::kServerSubscribeAccept:
		HandleServerSubscribeAccept(pData);
		break;
	case PacketType::kServerUnsubscribeAck:
		HandleServerUnsubscribeAck(pData);
		break;
	default:
		break;
	}
}

void NetworkClient::HandleServerAssignPlayer(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iPlayerIdValue = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	// Heap: received assignments vector grows on new assignment packets
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	mReceivedAssignments.push_back({game::player_t(uuid_t(iPlayerIdValue)), coord});

	common::Log("NetworkClient: Assigned player ID {} at grid ({},{})", iPlayerIdValue, coord.x, coord.y);
}

void NetworkClient::ClearSubscribingPlaceholder(GridCoord coord)
{
	for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (mCoordSlots[i].eState == CoordSubscriptionState::kSubscribing && mCoordSlots[i].coord == coord)
		{
			mCoordSlots[i] = {};
			break;
		}
	}
}

void NetworkClient::HandleServerCoordFullState(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iFrame = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	common::Log("NetworkClient: Received coord full state frame {} slot {} coord ({},{})", iFrame, uiSlotIndex, coord.x, coord.y);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	auto pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		common::Log("NetworkClient: LZ4 decompression failed for full state coord ({},{}) frame {}", coord.x, coord.y, iFrame);
		return;
	}

	// Validate slot before pushing full state
	if (uiSlotIndex >= NetworkManager::kiMaxCoordSlots)
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];

	// Full state can arrive before subscribe accept (different ENet channels)
	// If the slot is kUnsubscribed, the full state arrived first — clear the kSubscribing placeholder
	if (rSlot.eState == CoordSubscriptionState::kUnsubscribed)
	{
		ClearSubscribingPlaceholder(coord);
		rSlot.coord = coord;
	}
	else if (rSlot.eState == CoordSubscriptionState::kWaitingFullState || rSlot.eState == CoordSubscriptionState::kSubscribing)
	{
		// Validate coord matches to prevent stale full state from a previous subscription
		if (rSlot.coord != coord)
		{
			return;
		}
	}
	else
	{
		return;
	}

	ReceivedCoordFullState fullState {};
	fullState.iFrame = iFrame;
	fullState.coord = coord;
	fullState.iSlot = uiSlotIndex;
	fullState.pFrame = std::move(pFrame);

	// Heap: received full states vector grows on new cell data
	mReceivedFullStates.push_back(std::move(fullState));

	rSlot.iAckFloor = iFrame;
	rSlot.uiReceivedBitfield = 0;
	rSlot.uiEpoch = uiEpoch;
	rSlot.eState = CoordSubscriptionState::kActive;
	mReceivedCoordUpdates[uiSlotIndex].clear();
	FILE_LOG(0, "[NetworkClient] Slot {} now Active: coord=({},{}) ackFloor={}", uiSlotIndex, coord.x, coord.y, iFrame);
}

void NetworkClient::HandleServerCoordUpdateOrResend(const uint8_t* pData, bool bProcessRtt)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iFrame = ReadInt64(pCursor);

	// Pipeline RTT: read echoed client timestamp (monotonic guard prevents duplicate processing during multi-frame ticks)
	int64_t iEchoedTimestampNs = ReadInt64(pCursor);
	if (bProcessRtt && iEchoedTimestampNs > 0 && iEchoedTimestampNs > miLastEchoedTimestampNs)
	{
		miLastEchoedTimestampNs = iEchoedTimestampNs;
		int64_t iNowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		int64_t iRttUs = (iNowNs - iEchoedTimestampNs) / 1000;
		mSmoothedPipelineRttUs = iRttUs;
		mSmoothedPipelineRttUs.Update();
	}

	if (uiSlotIndex >= NetworkManager::kiMaxCoordSlots)
	{
		return;
	}
	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	if (rSlot.eState != CoordSubscriptionState::kActive || uiEpoch != rSlot.uiEpoch)
	{
		return;
	}

	common::crc_t serverCrc = static_cast<common::crc_t>(ReadUint64(pCursor));
	int32_t iCompressedSize = ReadInt32(pCursor);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedCoordUpdate update {};
	update.iFrame = iFrame;
	update.serverCrc = serverCrc;

	if (iCompressedSize > 0)
	{
		// Heap: status changes vector
		update.statusChanges.resize(kiMaxStatusChangesPerCell);
		int64_t iCount = DecompressStatusChangeBatch(pCursor, iCompressedSize, update.statusChanges.data(), kiMaxStatusChangesPerCell);
		update.statusChanges.resize(iCount);
		pCursor += iCompressedSize;
	}

	// Heap: player inputs vector
	ReadPlayerInputs(pCursor, update.playerInputs);

	// Heap: received updates vector grows each tick
	mReceivedCoordUpdates[uiSlotIndex].push_back(std::move(update));
	TrackReceivedFrame(uiSlotIndex, iFrame);

	FILE_LOG(0, "[NetworkClient] Coord{}: frame={} slot={}", bProcessRtt ? "Update" : "Resend", iFrame, uiSlotIndex);
}

void NetworkClient::TrackReceivedFrame(int64_t iSlot, int64_t iFrame)
{
	if (mbDesyncDebugMode)
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots[iSlot];

	// First frame received, initialize the ACK floor
	if (rSlot.iAckFloor < 0)
	{
		rSlot.iAckFloor = iFrame;
		return;
	}

	// Already acknowledged
	if (iFrame <= rSlot.iAckFloor)
	{
		return;
	}

	int64_t iBitIndex = iFrame - rSlot.iAckFloor - 1;
	if (iBitIndex >= kiMaxMissingFrames)
	{
		common::Log("NetworkClient: Too many missing frames on slot {} (gap={}), disconnecting", iSlot, iBitIndex + 1);
		FILE_LOG(0, "[NetworkClient] WARNING: Too many missing frames: slot={} gap={} ackFloor={} receivedFrame={}", iSlot, iBitIndex + 1, rSlot.iAckFloor, iFrame);
		DEBUG_BREAK();
		mbDisconnectedEvent = true;
		return;
	}

	// Mark this frame as received (only within the 64-bit bitfield range) and advance the floor past any contiguous run
	if (iBitIndex < 64)
	{
		rSlot.uiReceivedBitfield |= (1ULL << iBitIndex);
	}
	else
	{
		FILE_LOG(0, "[NetworkClient] BeyondBitfield: slot={} frame={} ackFloor={} bitIndex={} bitfield={:#x}", iSlot, iFrame, rSlot.iAckFloor, iBitIndex, rSlot.uiReceivedBitfield);
	}

	while (rSlot.uiReceivedBitfield & 1ULL)
	{
		++rSlot.iAckFloor;
		rSlot.uiReceivedBitfield >>= 1;
	}

	FILE_LOG(0, "[NetworkClient] AckAdvance: slot={} newFloor={} bitfield={:#x}", iSlot, rSlot.iAckFloor, rSlot.uiReceivedBitfield);
}

void NetworkClient::SendInput(const game::PlayerInput& rInput)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][4B sequence]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientInputStream));
	rWorkbuffer.PushBack<uint32_t>(++muiInputSequence);

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

	// Per-slot ACK state for proactive re-sends
	uint8_t uiAckSlotCount = 0;
	for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (mCoordSlots[i].eState == CoordSubscriptionState::kActive)
		{
			++uiAckSlotCount;
		}
	}
	rWorkbuffer.PushBack<uint8_t>(uiAckSlotCount);
	for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (mCoordSlots[i].eState == CoordSubscriptionState::kActive)
		{
			rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(i));
			rWorkbuffer.PushBack<uint16_t>(mCoordSlots[i].uiEpoch);
			rWorkbuffer.PushBack<int64_t>(mCoordSlots[i].iAckFloor);
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots[i].uiReceivedBitfield);
		}
	}

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

void NetworkClient::SendSubscribe(GridCoord coord)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	// Mark a local slot as kSubscribing so TrySubscribeNext gates until accept arrives
	bool bFoundSlot = false;
	for (int64_t i = 0; i < NetworkManager::kiMaxCoordSlots; ++i)
	{
		if (mCoordSlots[i].eState == CoordSubscriptionState::kUnsubscribed || mCoordSlots[i].eState == CoordSubscriptionState::kUnsubscribing)
		{
			// Purge delayed packets for the old slot's channels before reuse
			if constexpr (kbEnableNetworkSimulation)
			{
				if (mCoordSlots[i].eState == CoordSubscriptionState::kUnsubscribing)
				{
					uint8_t uiReliable = NetworkManager::CoordSlotReliable(i);
					uint8_t uiUnreliable = NetworkManager::CoordSlotUnreliable(i);
					std::erase_if(mDelayedPackets, [uiReliable, uiUnreliable](const DelayedPacket& rPacket)
					{
						return rPacket.uiChannelId == uiReliable || rPacket.uiChannelId == uiUnreliable;
					});
				}
			}

			mCoordSlots[i].coord = coord;
			mCoordSlots[i].eState = CoordSubscriptionState::kSubscribing;
			mCoordSlots[i].iAckFloor = -1;
			mCoordSlots[i].uiReceivedBitfield = 0;
			bFoundSlot = true;
			break;
		}
	}
	if (!bFoundSlot)
	{
		return;
	}

	FILE_LOG(0, "[NetworkClient] SendSubscribe: coord=({},{})", coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientSubscribe));
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::SendUnsubscribe(int64_t iSlot)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B slotIndex]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientUnsubscribe));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));

	FILE_LOG(0, "[NetworkClient] SendUnsubscribe: slot={}", iSlot);

	mCoordSlots[iSlot].eState = CoordSubscriptionState::kUnsubscribing;

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::HandleServerDebugFrame(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iFrame = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	common::Log("NetworkClient: Received debug frame {} grid ({},{})", iFrame, coord.x, coord.y);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	auto pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		common::Log("NetworkClient: LZ4 decompression failed for debug frame {}", iFrame);
		return;
	}

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

void NetworkClient::SendHello()
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientHello));
	rWorkbuffer.Append(std::string_view(kpcBuildConfigName));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void NetworkClient::HandleServerConnectionResponse(const uint8_t* pData, size_t iSize)
{
	const uint8_t* pCursor = pData + 1;
	bool bAccepted = (ReadUint8(pCursor) != 0);

	if (bAccepted)
	{
		mbConnectionAccepted = true;
		common::Log("NetworkClient: Connection accepted");
	}
	else
	{
		size_t iMessageLength = iSize - 2;
		size_t iCopyLength = std::min(iMessageLength, sizeof(mpcRejectionReason) - 1);
		std::memcpy(mpcRejectionReason, pCursor, iCopyLength);
		mpcRejectionReason[iCopyLength] = '\0';
		common::Log("NetworkClient: Connection rejected: {}", mpcRejectionReason);
	}
}

void NetworkClient::HandleServerSubscribeAccept(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	if (uiSlotIndex >= NetworkManager::kiMaxCoordSlots)
	{
		// Server rejected subscription (no free slot) — clear the kSubscribing placeholder
		ClearSubscribingPlaceholder(coord);
		common::Log("NetworkClient: Subscribe rejected for coord ({},{})", coord.x, coord.y);
		return;
	}

	// Validate target slot FIRST (before clearing placeholder)
	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	bool bTargetIsPlaceholder = (rSlot.eState == CoordSubscriptionState::kSubscribing && rSlot.coord == coord);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribed && !bTargetIsPlaceholder)
	{
		common::Log("NetworkClient: Subscribe accept for slot {} but slot is in state {}, ignoring", uiSlotIndex, static_cast<int>(rSlot.eState));
		return;
	}

	// Clear the client-side kSubscribing placeholder (may be at a different slot index than the server assigned)
	if (!bTargetIsPlaceholder)
	{
		ClearSubscribingPlaceholder(coord);
	}

	rSlot.coord = coord;
	rSlot.eState = CoordSubscriptionState::kWaitingFullState;
	rSlot.iAckFloor = -1;
	rSlot.uiReceivedBitfield = 0;
	rSlot.uiEpoch = uiEpoch;

	FILE_LOG(0, "[NetworkClient] SubscribeAccept: slot={} coord=({},{})", uiSlotIndex, coord.x, coord.y);
	common::Log("NetworkClient: Subscribe accepted slot {} coord ({},{})", uiSlotIndex, coord.x, coord.y);
}

void NetworkClient::HandleServerUnsubscribeAck(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	if (uiSlotIndex >= NetworkManager::kiMaxCoordSlots)
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribing)
	{
		return;
	}

	FILE_LOG(0, "[NetworkClient] UnsubscribeAck: slot={} coord=({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);
	common::Log("NetworkClient: Unsubscribe ack slot {} coord ({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);

	if constexpr (kbEnableNetworkSimulation)
	{
		uint8_t uiSlot = uiSlotIndex;
		std::erase_if(mDelayedPackets, [uiSlot](const DelayedPacket& rPacket)
		{
			return NetworkManager::IsCoordChannel(rPacket.uiChannelId) && NetworkManager::ChannelToSlot(rPacket.uiChannelId) == uiSlot;
		});
	}

	rSlot = {};
}

} // namespace engine
