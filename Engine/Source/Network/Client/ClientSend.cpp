#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

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
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots.at(i).ackState.uiReceivedBitfieldLow);
			rWorkbuffer.PushBack<uint64_t>(mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh);
		}
	}

	// Pipeline RTT: embed client timestamp for server to echo back
	int64_t iTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelUnreliable, rWorkbuffer, 0);

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

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

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
	WriteGridCoord(rWorkbuffer, coord);
	rWorkbuffer.PushBack<uint64_t>(expected);
	rWorkbuffer.PushBack<uint64_t>(actual);

	char pcExpected[20] {};
	char pcActual[20] {};
	Log(kLogNetwork, "Client::SendDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), expected), common::ToHex(std::span(pcActual), actual));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

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
	WriteGridCoord(rWorkbuffer, coord);

	Log(kLogNetwork, "Client::SendDebugFrameRequest Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

bool Client::SendSubscribe(GridCoord coord)
{
	if (!mbConnected || mpServerPeer == nullptr)
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
			mCoordSlots.at(i).ackState.uiReceivedBitfieldLow = 0;
			mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh = 0;
			bFoundSlot = true;
			Log(kLogNetwork, kVerbose, "Client::SendSubscribe Coord: ({},{}) Slot: {}", coord.x, coord.y, i);
			break;
		}
	}
	if (!bFoundSlot)
	{
		return false;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientSubscribe));
	WriteGridCoord(rWorkbuffer, coord);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
	return true;
}

void Client::SendUnsubscribe(int64_t iSlot)
{
	mCoordSlots.at(iSlot).eState = CoordSubscriptionState::kUnsubscribing;
	SendUnsubscribeOnly(iSlot);
}

void Client::SendUnsubscribeOnly(int64_t iSlot)
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

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendResyncRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientResyncRequest));

	Log(kLogNetwork, "Client::SendResyncRequest");

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendPauseRequest(bool bPaused)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B paused]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientPauseRequest));
	rWorkbuffer.PushBack<uint8_t>(bPaused ? 1 : 0);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendTimespeedRequest(uint8_t uiDirection)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B direction]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientTimespeedRequest));
	rWorkbuffer.PushBack<uint8_t>(uiDirection);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendSaveRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientSaveRequest));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendLoadRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientLoadRequest));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendResetRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientResetRequest));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendUpdatePlayerRequest(int64_t iGlobalPlayerId, bool bUseMissiles, float fNavigationDelay)
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientUpdatePlayerRequest));
	rWorkbuffer.PushBack<int64_t>(iGlobalPlayerId);
	rWorkbuffer.PushBack<uint8_t>(bUseMissiles ? 1 : 0);
	rWorkbuffer.PushBack<float>(fNavigationDelay);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	Log(kLogNetwork, "Client::SendUpdatePlayerRequest GlobalPlayer: {} Missiles: {} NavDelay: {}", iGlobalPlayerId, bUseMissiles, fNavigationDelay); // DT TEMP

	rWorkbuffer.Pop();
}

void Client::SendReplayRecordRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientReplayRecordRequest));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendReplayPlaybackRequest()
{
	if (!mbConnected || mpServerPeer == nullptr)
	{
		return;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientReplayPlaybackRequest));

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();
}

void Client::SendHello()
{
	// Heap: std::fstream and std::filesystem::path allocate for GUID file I/O
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	Log(kLogNetwork, "Client::SendHello"); // DT TEMP

	// Load GUID from disk if we don't have one yet
	if (mClientGuid.IsEmpty())
	{
		ClientGuid loadedGuid {};
		int64_t iGuidVersion = 0;
		std::fstream guidStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kRead}, std::filesystem::path("ClientGuid.bin"));
		if (guidStream.good())
		{
			common::Read(guidStream, iGuidVersion);
			int64_t iSize = 0;
			common::Read(guidStream, iSize);
			common::Read(guidStream, loadedGuid.uiHigh);
			common::Read(guidStream, loadedGuid.uiLow);
		}
		if (iGuidVersion >= 1 && !loadedGuid.IsEmpty())
		{
			mClientGuid = loadedGuid;
			Log("Client::SendHello Loaded GUID from disk: {} {}", mClientGuid.uiHigh, mClientGuid.uiLow);
		}
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kClientHello));
	rWorkbuffer.PushBack<uint32_t>(kuiProtocolVersion);
	rWorkbuffer.Append(std::string_view(kpcBuildConfigName));
	rWorkbuffer.PushBack<uint8_t>(0); // null terminator for config string
	rWorkbuffer.PushBack<uint64_t>(mClientGuid.uiHigh);
	rWorkbuffer.PushBack<uint64_t>(mClientGuid.uiLow);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	rWorkbuffer.Pop();

	Log(kLogNetwork, "Client::SendHello Complete"); // DT TEMP
}

} // namespace engine

#endif // BT_CLIENT
