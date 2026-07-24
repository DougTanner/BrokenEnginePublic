#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

#include "Game.h"

namespace engine
{

namespace
{

constexpr std::chrono::seconds kSubscriptionTransitionTimeout = 5s;

} // namespace

Client::Client(const char* pServerAddress, uint16_t uiPort, int64_t iCoordSlots, const ClientGuid& rGuid, GuidAssignedCallback pfnGuidAssigned)
{
	ASSERT(gpClient == nullptr);

	gpClient = this;

	mClientGuid = rGuid;
	mpfnGuidAssigned = pfnGuidAssigned;

	ScopedSuppressAllocationTracking suppress;

	mReceivedCoordUpdates.resize(iCoordSlots);
	mCoordSlots.resize(iCoordSlots);
	mStatusChangeScratch.resize(kiMaxStatusChangesPerCell);
	ENetAddress localAddress {};
	localAddress.host = htonl(INADDR_LOOPBACK);
	// Heap: ENet allocates host data internally
	mpHost = enet_host_create((gLaunchOptions.flags & LaunchOptionFlags::kLoopbackOnly) ? &localAddress : nullptr, 1, NetworkManager::kuiChannelCount, 0, 0);
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
	if (mpServerPeer != nullptr && (mStateFlags & ClientStateFlags::kConnected))
	{
		enet_peer_disconnect(mpServerPeer, 0);

		// Allow time for disconnect to be sent
		ENetEvent event {};
		// Heap: ENet service polls and may allocate event packets during disconnect drain
		ScopedSuppressAllocationTracking suppress;
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
		ScopedSuppressAllocationTracking suppress;
		enet_host_destroy(mpHost);
	}

	if (gpClient == this)
	{
		gpClient = nullptr;
	}
}

void Client::CancelSubscription(int64_t iSlot)
{
	GridCoord cancelledCoord = mCoordSlots.at(iSlot).coord;
	mCoordSlots.at(iSlot) = {};
	mCancelledSubscriptions.push_back(cancelledCoord);
	LOG(kNetwork, kVerbose, "Client::CancelSubscription Slot: {} Coord: ({},{}) CancelledCount: {}", iSlot, cancelledCoord.x, cancelledCoord.y, mCancelledSubscriptions.size());
}

void Client::ResetAllSlots()
{
	for (ClientCoordSlot& rSlot : mCoordSlots)
	{
		rSlot = {};
	}
	mCancelledSubscriptions.clear();
}

void Client::RecoverTimedOutSubscriptions()
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		ClientCoordSlot& rSlot = mCoordSlots.at(i);
		if (rSlot.eState == CoordSubscriptionState::kUnsubscribed
			|| rSlot.eState == CoordSubscriptionState::kActive
			|| now - rSlot.transitionStartTime < kSubscriptionTransitionTimeout)
		{
			continue;
		}

		switch (rSlot.eState)
		{
			case CoordSubscriptionState::kSubscribing:
			case CoordSubscriptionState::kUnsubscribing:
				rSlot = {};
				break;
			case CoordSubscriptionState::kWaitingFullState:
				SendUnsubscribe(i);
				break;
			case CoordSubscriptionState::kUnsubscribed:
			case CoordSubscriptionState::kActive:
				break;
		}
	}
}

void Client::Poll(const NetworkTimeState& rTimeState)
{
	ASSERT(common::gpMultithreading->IsMainThread());
	mTimeState = rTimeState;

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
				mStateFlags.Set(ClientStateFlags::kConnected);
				// Disable ENet peer throttle to prevent unreliable packet drops during reconciliation stalls
				enet_peer_throttle_configure(mpServerPeer, UINT32_MAX, 0, 0);
				SendHello();
				break;
			}
			case ENET_EVENT_TYPE_DISCONNECT:
				if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
				{
					// ENet can acknowledge a reliable rejection before the simulated receive delay releases it.
					// Deliver that rejection before the disconnect tears down the session and its delay queue.
					auto it = std::ranges::find_if(mDelayedPackets, [](const DelayedPacket& rPacket)
					{
						return rPacket.data.size() >= 2
							&& static_cast<PacketType>(rPacket.data.at(0)) == PacketType::kServerConnectionResponse
							&& rPacket.data.at(1) == 0;
					});
					if (it != mDelayedPackets.end())
					{
						Receive(it->data);
						mDelayedPackets.erase(it);
					}
				}
				mStateFlags.Clear(ClientStateFlags::kConnected);
				mStateFlags.Set(ClientStateFlags::kDisconnectedEvent);
				mpServerPeer = nullptr;
				LOG(kNetwork, kInfo, "ENET_EVENT_TYPE_DISCONNECT");
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
			Receive(rPacket.data);
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

void Client::DispatchIncoming(ENetEvent& rEvent, bool bFastForward)
{
	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		constexpr NetworkSimulationConfig kSimConfig = GetNetworkSimulationConfig(keNetworkSimulation);
		NetworkSimulation::DispatchOrEnqueue(mDelayedPackets, mNetworkSimState, kSimConfig, bFastForward, rEvent,
			[this](ENetEvent& rInner) { Receive(rInner); });
	}
	else
	{
		Receive(rEvent);
		enet_packet_destroy(rEvent.packet);
	}
}

void Client::Receive(ENetEvent& rEvent)
{
	Receive(std::span<const uint8_t>(rEvent.packet->data, rEvent.packet->dataLength));
}

void Client::Receive(std::span<const uint8_t> packetData)
{
	if (packetData.size() < 1)
	{
		return;
	}

	PacketType eType = static_cast<PacketType>(packetData[0]);

	try
	{
		switch (eType)
		{
			case PacketType::kServerCoordFullState:
				ServerCoordFullState(packetData);
				break;
			case PacketType::kServerCoordStaticData:
				ServerCoordStaticData(packetData);
				break;
			case PacketType::kServerCoordUpdate:
				ServerCoordUpdateOrResend(packetData, true);
				break;
			case PacketType::kServerCoordResend:
				ServerCoordUpdateOrResend(packetData, false);
				break;
			case PacketType::kServerDebugFrame:
				ServerDebugFrame(packetData);
				break;
			case PacketType::kServerConnectionResponse:
				ServerConnectionResponse(packetData);
				break;
			case PacketType::kServerSubscribeAccept:
				ServerSubscribeAccept(packetData);
				break;
			case PacketType::kServerUnsubscribeAck:
				ServerUnsubscribeAck(packetData);
				break;
			case PacketType::kServerLoadNotification:
				ServerLoadNotification(packetData);
				break;
			default:
				if (static_cast<uint8_t>(eType) >= static_cast<uint8_t>(PacketType::kGamePacketStart))
				{
					ScopedSuppressAllocationTracking suppress;
					// Heap: raw game packet buffer grows on game-specific packets
					mReceivedGamePackets.emplace_back(packetData[0], std::vector<uint8_t>(packetData.begin() + 1, packetData.end()));
				}
				else
				{
					LOG(kNetwork, kWarning, "Client::Receive unknown packet type {}", static_cast<uint8_t>(eType));
				}
				break;
		}
	}
	catch (const std::exception& rException)
	{
		// Trust boundary: a corrupt count/size in a received payload throws CorruptStreamException
		// (or .at()/bad_alloc) from the reader before any slot state is mutated (full-state/static
		// reads land in locals first). Drop the single packet and let the server resend, rather than
		// tearing down the client.
		LOG(kNetwork, kWarning, "Client::Receive dropped corrupt packet (type {}): {}", static_cast<uint8_t>(eType), rException.what());
	}
}

void Client::TrackReceivedTick(int64_t iSlot, int64_t iTick)
{
	if (mStateFlags & ClientStateFlags::kDesyncDebugMode)
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
		mStateFlags.Set(ClientStateFlags::kDisconnectedEvent);
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
	while ((rAck.uiReceivedBitfieldLow & 1ULL) != 0u)
	{
		++rAck.iAckFloor;
		rAck.uiReceivedBitfieldLow >>= 1;
		if ((rAck.uiReceivedBitfieldHigh & 1ULL) != 0u)
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
	if (mpServerPeer != nullptr && (mStateFlags & ClientStateFlags::kConnected))
	{
		// Heap: ENet may queue a peer disconnect packet
		ScopedSuppressAllocationTracking suppress;
		enet_peer_disconnect(mpServerPeer, 0);
		mStateFlags.Clear(ClientStateFlags::kConnected);
	}
}

} // namespace engine

#endif // BT_CLIENT
