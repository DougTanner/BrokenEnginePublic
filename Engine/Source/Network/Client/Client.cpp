#include "Pch.h"

#include "Network/Client/Client.h"

#include "Network/NetworkCursor.h"

namespace engine
{

Client::Client(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots)
{
	gpClient = this;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	mReceivedCoordUpdates.resize(iCoordSlots);
	mCoordSlots.resize(iCoordSlots);
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
}

Client::~Client()
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

	gpClient = nullptr;
}

void Client::Poll()
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
	mReceivedGamePackets.clear();

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
				Log(kLogNetwork, "Client::Poll ENET_EVENT_TYPE_CONNECT");
				ScopedLogIndent scopedLogIndent;
				char pcServerAddress[64] {};
				enet_address_get_host_ip(&mpServerPeer->address, pcServerAddress, sizeof(pcServerAddress));
				ENetAddress localAddress {};
				enet_socket_get_address(mpHost->socket, &localAddress);
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT:
				mbConnected = false;
				mbDisconnectedEvent = true;
				mpServerPeer = nullptr;
				Log(kLogNetwork, "ENET_EVENT_TYPE_DISCONNECT");
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
			Receive(rPacket.data.data(), rPacket.data.size());
		});
	}

	// Track bandwidth deltas from host-level cumulative counters
	uint32_t uiReceivedData = mpHost->totalReceivedData;
	uint32_t uiSentData = mpHost->totalSentData;
	mBytesInPerSecond.Set(static_cast<int64_t>(uiReceivedData - muiPrevReceivedData));
	mBytesOutPerSecond.Set(static_cast<int64_t>(uiSentData - muiPrevSentData));
	muiPrevReceivedData = uiReceivedData;
	muiPrevSentData = uiSentData;
}

void Client::Receive(ENetEvent& rEvent)
{
	Receive(rEvent.packet->data, rEvent.packet->dataLength);
}

void Client::Receive(const uint8_t* pData, size_t iSize)
{
	if (iSize < 1)
	{
		return;
	}

	PacketType eType = static_cast<PacketType>(pData[0]);

	switch (eType)
	{
		case PacketType::kServerAssignPlayer:
		case PacketType::kServerPlayerState:
		{
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			// Heap: raw game packet buffer grows on game-specific packets
			mReceivedGamePackets.emplace_back(pData[0], std::vector<uint8_t>(pData + 1, pData + iSize));
			break;
		}
		case PacketType::kServerCoordFullState:
			ServerCoordFullState(pData);
			break;
		case PacketType::kServerCoordUpdate:
			ServerCoordUpdateOrResend(pData, true);
			break;
		case PacketType::kServerCoordResend:
			ServerCoordUpdateOrResend(pData, false);
			break;
		case PacketType::kServerDebugFrame:
			ServerDebugFrame(pData);
			break;
		case PacketType::kServerConnectionResponse:
			ServerConnectionResponse(pData, iSize);
			break;
		case PacketType::kServerSubscribeAccept:
			ServerSubscribeAccept(pData);
			break;
		case PacketType::kServerUnsubscribeAck:
			ServerUnsubscribeAck(pData);
			break;
		default:
			break;
	}
}

void Client::TrackReceivedTick(int64_t iSlot, int64_t iTick)
{
	if (mbDesyncDebugMode)
	{
		return;
	}

	AckState& rAck = mCoordSlots.at(iSlot).ackState;

	// First frame received, initialize the ACK floor
	if (rAck.iAckFloor < 0)
	{
		rAck.iAckFloor = iTick;
		return;
	}

	// Already acknowledged
	if (iTick <= rAck.iAckFloor)
	{
		return;
	}

	int64_t iBitIndex = iTick - rAck.iAckFloor - 1;
	if (iBitIndex >= kiTickRate)
	{
		Log(kLogNetwork, "Client::TrackReceivedTick Too many missing frames, disconnecting Slot: {} Gap: {}", iSlot, iBitIndex + 1);
		DEBUG_BREAK();
		mbDisconnectedEvent = true;
		return;
	}

	// Mark this frame as received and advance the floor past any contiguous run
	rAck.uiReceivedBitfield |= (1ULL << iBitIndex);

	while (rAck.uiReceivedBitfield & 1ULL)
	{
		++rAck.iAckFloor;
		rAck.uiReceivedBitfield >>= 1;
	}

}

void Client::Flush()
{
	enet_host_flush(mpHost);
}

void Client::Disconnect()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		enet_peer_disconnect(mpServerPeer, 0);
		mbConnected = false;
	}
}

} // namespace engine
