#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

namespace engine
{

void Client::SendAck()
{
	if (!CanSend())
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

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
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots.at(i).ackState.uiReceivedBitfieldLow);
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh);
		}
	}

	// Pipeline RTT: embed client timestamp for server to echo back
	int64_t iTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelUnreliable, rWorkbuffer, 0);
}

void Client::SendSpawnRequest(ClientRequestFlags_t flags)
{
	if (!CanSend())
	{
		return;
	}

	uint8_t uiFlags = 0;
	std::memcpy(&uiFlags, &flags, sizeof(uint8_t));

	LOG(kNetwork, kDebug, "Client::SendSpawnRequest Spawn: {} Respawn: {}", static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	SendSimplePacket(PacketType::kClientSpawnRequest, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, uiFlags);
}

void Client::SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual)
{
	if (!CanSend())
	{
		return;
	}

	char pcExpected[20] {};
	char pcActual[20] {};
	LOG(kNetwork, kError, "Client::SendDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), expected), common::ToHex(std::span(pcActual), actual));

	SendSimplePacket(PacketType::kClientDesyncReport, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, iTick, coord, static_cast<uint64_t>(expected), static_cast<uint64_t>(actual));
}

void Client::SendDebugFrameRequest(int64_t iTick, GridCoord coord)
{
	if (!CanSend())
	{
		return;
	}

	LOG(kNetwork, kError, "Client::SendDebugFrameRequest Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);

	// [1B type][8B frame][4B gridX][4B gridY]
	SendSimplePacket(PacketType::kClientDebugFrameRequest, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, iTick, coord);
}

bool Client::SendSubscribe(GridCoord coord)
{
	if (!CanSend())
	{
		return false;
	}

	// Mark a local slot as kSubscribing
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
					NetworkSimulation::PurgeDelayedForSlot(mDelayedPackets, i);
				}
			}

			mCoordSlots.at(i).coord = coord;
			mCoordSlots.at(i).eState = CoordSubscriptionState::kSubscribing;
			mCoordSlots.at(i).ackState.iAckFloor = -1;
			mCoordSlots.at(i).ackState.uiReceivedBitfieldLow = 0;
			mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh = 0;
			bFoundSlot = true;
			break;
		}
	}
	if (!bFoundSlot)
	{
		return false;
	}

	// [1B type][4B coord.x][4B coord.y]
	SendSimplePacket(PacketType::kClientSubscribe, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, coord);

	return true;
}

void Client::SendUnsubscribe(int64_t iSlot)
{
	mCoordSlots.at(iSlot).eState = CoordSubscriptionState::kUnsubscribing;
	SendSimplePacket(PacketType::kClientUnsubscribe, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(iSlot));
}

void Client::SendResyncRequest()
{
	if (!CanSend())
	{
		return;
	}

	LOG(kNetwork, kError, "Client::SendResyncRequest");

	SendSimplePacket(PacketType::kClientResyncRequest, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
}

void Client::SendHello()
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientHello));
	rWorkbuffer.PushBack<uint32_t>(kuiProtocolVersion);
	rWorkbuffer.PushBack<int64_t>(game::Frame::kiVersion);
	rWorkbuffer.Append(std::string_view(kpcBuildConfigName));
	rWorkbuffer.PushBack<uint8_t>(0); // null terminator for config string
	rWorkbuffer.PushBack<uint64_t>(mClientGuid.uiHigh);
	rWorkbuffer.PushBack<uint64_t>(mClientGuid.uiLow);

	miHelloSendTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

} // namespace engine

#endif // BT_CLIENT
