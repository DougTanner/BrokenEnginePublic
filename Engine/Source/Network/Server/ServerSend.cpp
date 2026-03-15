#include "Pch.h"

#include "Network/Server/Server.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

void Server::SendAssignPlayer(int64_t iClientId, int64_t iPlayerId, GridCoord coord)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	pClient->humanPlayerId = game::player_t(uuid_t(iPlayerId));
	pClient->humanGridCoord = coord;

	Log(kLogNetwork, "Server::SendAssignPlayer Client: {} Player: {} Grid: ({},{})", iClientId, iPlayerId, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B player_t ID][GridCoord]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerAssignPlayer));
	rWorkbuffer.PushBack<int64_t>(iPlayerId);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pClient->pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void Server::SendPlayerState(int64_t iClientId, uint8_t uiStateType, int64_t iPlayerId, GridCoord coord)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	Log(kLogNetwork, "Server::SendPlayerState State: {} Client: {} Player: {} Grid: ({},{})", static_cast<int>(uiStateType), iClientId, iPlayerId, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B state][8B player_t ID][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerPlayerState));
	rWorkbuffer.PushBack<uint8_t>(uiStateType);
	rWorkbuffer.PushBack<int64_t>(iPlayerId);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pClient->pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void Server::SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iTick, GridCoord coord, const game::Frame* pFrame)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	Log(kLogNetwork, "Server::SendCoordFullState Client: {} Frame: {} Slot: {} Coord: ({},{})", iClientId, iTick, iSlot, coord.x, coord.y);

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
	rWorkbuffer.PushBack<uint16_t>(pClient->coordAckStates.at(iSlot).uiEpoch);
	rWorkbuffer.PushBack<int64_t>(iTick);
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

void Server::SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage)
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

void Server::SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][1B slotIndex][2B epoch][4B coord.x][4B coord.y]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerSubscribeAccept));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	uint16_t uiEpoch = (iSlot < std::ssize(rClient.coordSubscriptions)) ? rClient.coordAckStates.at(iSlot).uiEpoch : 0;
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

void Server::SendUnsubscribeAck(ClientConnection& rClient, int64_t iSlot)
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

void Server::WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs)
{
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(uiEpoch);
	rWorkbuffer.PushBack<int64_t>(rBuffered.iTick);
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);
	rWorkbuffer.PushBack<uint64_t>(rBuffered.serverCrc);
	rWorkbuffer.PushBack<uint64_t>(rBuffered.inputCrc);

	int32_t iCompressedSize = static_cast<int32_t>(rBuffered.compressedData.size());
	rWorkbuffer.PushBack<int32_t>(iCompressedSize);
	if (iCompressedSize > 0)
	{
		rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rBuffered.compressedData.data()), iCompressedSize));
	}
}

void Server::SendUpdate(ClientConnection& rClient, int64_t iTick)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	int64_t iSent = 0; // DT: TEMP

	// Send one packet per active subscription slot
	for (int64_t iSlot = 0; iSlot < std::ssize(rClient.coordSubscriptions); ++iSlot)
	{
		if (!rClient.coordSubscriptions.at(iSlot).bActive)
		{
			continue;
		}

		GridCoord coord = rClient.coordSubscriptions.at(iSlot).coord;

		const PerCoordBufferedFrame* pBuffered = FindBufferedFrame(coord, iTick);
		if (pBuffered == nullptr)
		{
			continue;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		rWorkbuffer.Push();

		WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordUpdate, iSlot, rClient.coordAckStates.at(iSlot).uiEpoch, *pBuffered, rClient.iClientTimestampNs);

		std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

		// Heap: ENet allocates packet data internally
		ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
		enet_peer_send(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), pPacket);

		rWorkbuffer.Pop();
		++iSent; // DT: TEMP
	}

	Log(kLogNetwork, "Server::SendUpdate client: {} tick: {} sent: {}", rClient.iClientId, iTick, iSent); // DT: TEMP
}

void Server::SendResends(ClientConnection& rClient, int64_t iTick)
{
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// Iterate per-slot: each active subscription has its own ACK state and coord ring buffer
	for (int64_t iSlot = 0; iSlot < std::ssize(rClient.coordSubscriptions); ++iSlot)
	{
		if (!rClient.coordSubscriptions.at(iSlot).bActive)
		{
			continue;
		}

		const AckState& rAck = rClient.coordAckStates.at(iSlot);
		if (rAck.iAckFloor < 0)
		{
			continue;
		}

		GridCoord coord = rClient.coordSubscriptions.at(iSlot).coord;

		if (rAck.uiReceivedBitfieldLow == 0 && rAck.uiReceivedBitfieldHigh == 0)
		{
			continue;
		}
		int64_t iScanLimit = (rAck.uiReceivedBitfieldHigh != 0)
			? std::max(static_cast<int64_t>(std::bit_width(rAck.uiReceivedBitfieldLow)), 64 + static_cast<int64_t>(std::bit_width(rAck.uiReceivedBitfieldHigh)))
			: static_cast<int64_t>(std::bit_width(rAck.uiReceivedBitfieldLow));

		int64_t iSlotResendCount = 0;
		for (int64_t iBit = 0; iBit < iScanLimit && iSlotResendCount < kiMaxResendFrames; ++iBit)
		{
			bool bReceived = (iBit < 64) ? (rAck.uiReceivedBitfieldLow & (1ULL << iBit)) != 0 : (rAck.uiReceivedBitfieldHigh & (1ULL << (iBit - 64))) != 0;
			if (bReceived)
			{
				continue;
			}

			int64_t iMissingFrame = rAck.iAckFloor + 1 + iBit;
			if (iMissingFrame >= iTick)
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

			WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordResend, iSlot, rClient.coordAckStates.at(iSlot).uiEpoch, *pBuffered, rClient.iClientTimestampNs);

			std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

			// Heap: ENet allocates packet data internally
			ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
			enet_peer_send(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), pPacket);

			rWorkbuffer.Pop();

			++iSlotResendCount;
		}

		if (iSlotResendCount > 0)
		{
			Log(kLogNetwork, "Server::SendResends Client: {} Slot: {} Coord: ({},{}) Count: {}", rClient.iClientId, iSlot, coord.x, coord.y, iSlotResendCount);
		}
	}
}

} // namespace engine
