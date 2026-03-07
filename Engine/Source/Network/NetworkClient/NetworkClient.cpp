#include "Pch.h"

#include "Network/NetworkClient/NetworkClient.h"

#include "Network/NetworkCursor.h"

namespace engine
{

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
	mReceivedPlayerStates.clear();

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
			HandleReceive(mDelayedPackets.front().data.data(), mDelayedPackets.front().data.size());
			mDelayedPackets.pop_front();
		}
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
	case PacketType::kServerPlayerState:
		HandleServerPlayerState(pData);
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
