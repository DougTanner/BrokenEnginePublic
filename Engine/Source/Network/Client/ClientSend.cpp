#include "Pch.h"

#include "Network/Client/Client.h"

#include "Network/NetworkCursor.h"

namespace engine
{

void Client::SendAck()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientAckStream));

	// Per-slot ACK state for proactive re-sends
	uint8_t uiAckSlotCount = 0;
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kActive)
		{
			++uiAckSlotCount;
		}
	}
	rWorkbuffer.PushBack<uint8_t>(uiAckSlotCount);
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kActive)
		{
			rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(i));
			rWorkbuffer.PushBack<uint16_t>(mCoordSlots.at(i).ackState.uiEpoch);
			rWorkbuffer.PushBack<int64_t>(mCoordSlots.at(i).ackState.iAckFloor);
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots.at(i).ackState.uiReceivedBitfield);
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

void Client::SendSpawnRequest(ClientRequestFlags_t flags)
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

	Log(kLogNetwork, "Client::SendSpawnRequest Spawn: {} Respawn: {}", static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void Client::SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientDesyncReport));
	rWorkbuffer.PushBack<int64_t>(iTick);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<uint64_t>(expected);
	rWorkbuffer.PushBack<uint64_t>(actual);

	char pcExpected[20] {};
	char pcActual[20] {};
	Log(kLogNetwork, "Client::SendDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), expected), common::ToHex(std::span(pcActual), actual));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void Client::SendDebugFrameRequest(int64_t iTick, GridCoord coord)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B frame][4B gridX][4B gridY]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientDebugFrameRequest));
	rWorkbuffer.PushBack<int64_t>(iTick);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	Log(kLogNetwork, "Client::SendDebugFrameRequest Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void Client::SendSubscribe(GridCoord coord)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	// Mark a local slot as kSubscribing so TrySubscribeNext gates until accept arrives
	bool bFoundSlot = false;
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribed || mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
		{
			// Purge delayed packets for the old slot's channels before reuse
			if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
			{
				if (mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
				{
					uint8_t uiReliable = NetworkManager::CoordSlotReliable(i);
					uint8_t uiUnreliable = NetworkManager::CoordSlotUnreliable(i);
					std::erase_if(mDelayedPackets, [uiReliable, uiUnreliable](const DelayedPacket& rPacket)
					{
						return rPacket.uiChannelId == uiReliable || rPacket.uiChannelId == uiUnreliable;
					});
				}
			}

			mCoordSlots.at(i).coord = coord;
			mCoordSlots.at(i).eState = CoordSubscriptionState::kSubscribing;
			mCoordSlots.at(i).ackState.iAckFloor = -1;
			mCoordSlots.at(i).ackState.uiReceivedBitfield = 0;
			bFoundSlot = true;
			break;
		}
	}
	if (!bFoundSlot)
	{
		return;
	}

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

void Client::SendUnsubscribe(int64_t iSlot)
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

	mCoordSlots.at(iSlot).eState = CoordSubscriptionState::kUnsubscribing;

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	{
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
		enet_peer_send(mpServerPeer, NetworkManager::kuiChannelReliable, pPacket);
	}

	rWorkbuffer.Pop();
}

void Client::SendHello()
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientHello));
	rWorkbuffer.PushBack<uint32_t>(kuiProtocolVersion);
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

} // namespace engine
