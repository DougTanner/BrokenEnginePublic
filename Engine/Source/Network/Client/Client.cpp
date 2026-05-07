#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Network/NetworkCursor.h"

namespace engine
{

Client::Client(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots)
{
	gpClient = this;

	ScopedSuppressAllocationTracking suppressAllocationTracking;

	mReceivedCoordUpdates.resize(iCoordSlots);
	mCoordSlots.resize(iCoordSlots);
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create(nullptr, 1, NetworkManager::kuiChannelCount, 0, 0);
	if (mpHost == nullptr)
	{
		LOG(kNetwork, kWarning, "Client::Client enet_host_create failed");
		return;
	}
	// 1MB send/receive buffers to handle bursty packet traffic
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_SNDBUF, 1024 * 1024);
	enet_socket_set_option(mpHost->socket, ENET_SOCKOPT_RCVBUF, 1024 * 1024);

	ENetAddress address {};
	enet_address_set_host(&address, pServerAddress);
	address.port = uiPort;

	// Heap: ENet allocates peer data internally
	mpServerPeer = enet_host_connect(mpHost, &address, NetworkManager::kuiChannelCount, 0);
}

Client::~Client()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		enet_peer_disconnect(mpServerPeer, 0);

		// Allow time for disconnect to be sent
		ENetEvent event {};
		// Heap: ENet service polls and may allocate event packets during disconnect drain
		ScopedSuppressAllocationTracking suppressAllocationTracking;
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
		// Heap: ENet destroys host data internally
		ScopedSuppressAllocationTracking suppressAllocationTracking;
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
	mReceivedStaticData.clear();
	mReceivedGamePackets.clear();

	ENetEvent event {};
	while (enet_host_service(mpHost, &event, 0) > 0)
	{
		switch (event.type)
		{
			case ENET_EVENT_TYPE_CONNECT:
			{
				LOG(kNetwork, kInfo, "Client::Poll ENET_EVENT_TYPE_CONNECT");
				mbConnected = true;
				// Disable ENet peer throttle to prevent unreliable packet drops during reconciliation stalls
				enet_peer_throttle_configure(mpServerPeer, UINT32_MAX, 0, 0);
				SendHello();
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT:
				mbConnected = false;
				mbDisconnectedEvent = true;
				mpServerPeer = nullptr;
				LOG(kNetwork, kInfo, "ENET_EVENT_TYPE_DISCONNECT");
				break;
			case ENET_EVENT_TYPE_RECEIVE:
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					if (game::gpGame->mTimeStep.miTimeMultiply > 1)
					{
						Receive(event);
						enet_packet_destroy(event.packet);
					}
					else
					{
						constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
						NetworkSimulation::EnqueueOrDrop(mDelayedPackets, kSimConfig, event,
							[this](ENetEvent& rEvent) { Receive(rEvent); });
					}
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

	// Process delayed packets whose release time has passed (or flush all when bypassing simulation)
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		auto handleDelayed = [this](const DelayedPacket& rPacket)
		{
			Receive(rPacket.data.data(), rPacket.data.size());
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
		case PacketType::kServerCoordFullState:
			ServerCoordFullState(pData, iSize);
			break;
		case PacketType::kServerCoordStaticData:
			ServerCoordStaticData(pData, iSize);
			break;
		case PacketType::kServerCoordUpdate:
			ServerCoordUpdateOrResend(pData, iSize, true);
			break;
		case PacketType::kServerCoordResend:
			ServerCoordUpdateOrResend(pData, iSize, false);
			break;
		case PacketType::kServerDebugFrame:
			ServerDebugFrame(pData, iSize);
			break;
		case PacketType::kServerConnectionResponse:
			ServerConnectionResponse(pData, iSize);
			break;
		case PacketType::kServerSubscribeAccept:
			ServerSubscribeAccept(pData, iSize);
			break;
		case PacketType::kServerUnsubscribeAck:
			ServerUnsubscribeAck(pData, iSize);
			break;
		case PacketType::kServerTimespeedUpdate:
			ServerTimespeedUpdate(pData, iSize);
			break;
		case PacketType::kServerLoadNotification:
			mbLoadNotificationReceived = true;
			break;
		default:
			if (static_cast<uint8_t>(eType) >= static_cast<uint8_t>(PacketType::kGamePacketStart))
			{
				ScopedSuppressAllocationTracking suppressAllocationTracking;
				// Heap: raw game packet buffer grows on game-specific packets
				mReceivedGamePackets.emplace_back(pData[0], std::vector<uint8_t>(pData + 1, pData + iSize));
			}
			else
			{
				LOG(kNetwork, kWarning, "Client::Receive unknown packet type {}", static_cast<uint8_t>(eType));
			}
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
		mFramesReceived.Set(1);
		return;
	}

	// Already acknowledged
	if (iTick <= rAck.iAckFloor)
	{
		return;
	}

	int64_t iBitIndex = iTick - rAck.iAckFloor - 1;
	if (iBitIndex >= kiNetworkBufferSize)
	{
		LOG(kNetwork, kWarning, "Client::TrackReceivedTick Too many missing frames, disconnecting Slot: {} Gap: {}", iSlot, iBitIndex + 1);
		mbDisconnectedEvent = true;
		return;
	}

	// Mark this frame as received and advance the floor past any contiguous run
	if (iBitIndex < 64)
	{
		rAck.uiReceivedBitfieldLow |= (1ULL << iBitIndex);
	}
	else
	{
		rAck.uiReceivedBitfieldHigh |= (1ULL << (iBitIndex - 64));
	}
	mFramesReceived.Set(1);

	int64_t iPreviousFloor = rAck.iAckFloor;
	while (rAck.uiReceivedBitfieldLow & 1ULL)
	{
		++rAck.iAckFloor;
		rAck.uiReceivedBitfieldLow >>= 1;
		if (rAck.uiReceivedBitfieldHigh & 1ULL)
		{
			rAck.uiReceivedBitfieldLow |= (1ULL << 63);
		}
		rAck.uiReceivedBitfieldHigh >>= 1;
	}
	if (rAck.iAckFloor != iPreviousFloor && rAck.iAckFloor - iPreviousFloor > 50)
	{
		LOG(kNetwork, kVerbose, "Client::TrackReceivedTick FloorAdvance Slot: {} Floor: {} -> {} Delta: {}", iSlot, iPreviousFloor, rAck.iAckFloor, rAck.iAckFloor - iPreviousFloor);
	}
}

void Client::Flush()
{
	if (mpHost == nullptr)
	{
		return;
	}
	enet_host_flush(mpHost);
}

void Client::Disconnect()
{
	if (mpServerPeer != nullptr && mbConnected)
	{
		// Heap: ENet may queue a peer disconnect packet
		ScopedSuppressAllocationTracking suppressAllocationTracking;
		enet_peer_disconnect(mpServerPeer, 0);
		mbConnected = false;
	}
}

float Client::GetPacketLossPercent()
{
	int64_t iActiveSlots = 0;
	for (const ClientCoordSlot& rSlot : mCoordSlots)
	{
		if (rSlot.eState == CoordSubscriptionState::kActive)
		{
			++iActiveSlots;
		}
	}
	int64_t iExpected = kiTickRate * iActiveSlots;
	if (iExpected <= 0)
	{
		return 0.0f;
	}
	int64_t iReceived = mFramesReceived.Get();
	int64_t iLost = iExpected - iReceived;
	if (iLost <= 0)
	{
		return 0.0f;
	}
	return static_cast<float>(iLost) * 100.0f / static_cast<float>(iExpected);
}

} // namespace engine

#endif // BT_CLIENT
