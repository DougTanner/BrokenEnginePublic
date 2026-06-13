#include "Pch.h"

#include "Network/Server/Server.h"
#include "Frame/FrameStaticData.h"
#include "Memory/GlobalAllocator.h"
#include "Network/NetworkCursor.h"

namespace engine
{

void Server::SendCoordFullState(int64_t iClientId, int64_t iSlot, int64_t iTick, GridCoord coord, const game::Frame* pFrame)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kDebug, "Server::SendCoordFullState Client: {} Frame: {} Slot: {} Coord: ({},{})", iClientId, iTick, iSlot, coord.x, coord.y);

	// Serialize frame to a temporary stringstream
	ScopedSuppressAllocationTracking suppress;
	// Heap: stringstream allocates for frame serialization
	std::ostringstream frameStream(std::ios::binary);
	frameStream << *pFrame;
	std::string frameData = frameStream.str();

	// LZ4 compress
	int iCompressedSize = CompressToBuffer(frameData.data(), static_cast<int>(frameData.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// [1B type][1B slotIndex][2B epoch][8B frame][4B coord.x][4B coord.y][4B uncompressedSize][4B compressedSize][...LZ4 data]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerCoordFullState));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(pClient->coordAckStates.at(iSlot).uiEpoch);
	rWorkbuffer.PushBack<int64_t>(iTick);
	WriteGridCoord(rWorkbuffer, coord);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(frameData.size()));
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
	rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(mCompressionBuffer.data()), iCompressedSize));

	NetworkManager::SendPacket(pClient->pPeer, NetworkManager::CoordSlotReliable(iSlot), rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::SendCoordStaticData(int64_t iClientId, int64_t iSlot, GridCoord coord, const FrameStaticData& rStaticData)
{
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kDebug, "Server::SendCoordStaticData Client: {} Slot: {} Coord: ({},{})", iClientId, iSlot, coord.x, coord.y);

	// Heap: stringstream allocates for static data serialization
	ScopedSuppressAllocationTracking suppress;
	std::ostringstream staticStream(std::ios::binary);
	rStaticData.Write(staticStream, /*bIncludeNavData=*/true);
	std::string staticData = staticStream.str();

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// [1B type][1B slot][2B epoch][4B coord.x][4B coord.y][4B size][...staticData bytes]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerCoordStaticData));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(pClient->coordAckStates.at(iSlot).uiEpoch);
	WriteGridCoord(rWorkbuffer, coord);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(staticData.size()));
	rWorkbuffer.Append(std::string_view(staticData));

	NetworkManager::SendPacket(pClient->pPeer, NetworkManager::CoordSlotReliable(iSlot), rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage, const ClientGuid* pGuid)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerConnectionResponse));
	rWorkbuffer.PushBack<uint8_t>(bAccepted ? 1 : 0);
	if (bAccepted && pGuid != nullptr)
	{
		rWorkbuffer.PushBack<uint64_t>(pGuid->uiHigh);
		rWorkbuffer.PushBack<uint64_t>(pGuid->uiLow);
	}
	else if (!bAccepted && pMessage != nullptr)
	{
		rWorkbuffer.Append(std::string_view(pMessage));
	}

	NetworkManager::SendPacket(pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord)
{
	// [1B type][1B slotIndex][2B epoch][4B coord.x][4B coord.y]
	uint16_t uiEpoch = (iSlot < std::ssize(rClient.coordSubscriptions)) ? rClient.coordAckStates.at(iSlot).uiEpoch : 0;
	SendSimplePacket(rClient.pPeer, PacketType::kServerSubscribeAccept, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, static_cast<uint8_t>(iSlot), uiEpoch, coord);
}

void Server::WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs)
{
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(eType));
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
	rWorkbuffer.PushBack<uint16_t>(uiEpoch);
	rWorkbuffer.PushBack<int64_t>(rBuffered.iTick);
	rWorkbuffer.PushBack<int64_t>(iTimestampNs);
	rWorkbuffer.PushBack<uint64_t>(rBuffered.sharedCrc);

	int32_t iCompressedSize = static_cast<int32_t>(rBuffered.compressedData.size());
	rWorkbuffer.PushBack<int32_t>(iCompressedSize);
	if (iCompressedSize > 0)
	{
		rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rBuffered.compressedData.data()), iCompressedSize));
	}
}

void Server::SendUpdate(ClientConnection& rClient, int64_t iTick)
{
	// Heap: workbuffer Push (per-slot scope) and ENet packet creation in SendPacket
	ScopedSuppressAllocationTracking suppress;

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
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordUpdate, iSlot, rClient.coordAckStates.at(iSlot).uiEpoch, *pBuffered, rClient.iClientTimestampNs);

		NetworkManager::SendPacket(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), rWorkbuffer, 0);
		if (!rClient.coordSubscriptions.at(iSlot).bFirstUpdateLogged)
		{
			LOG(kNetwork, kVerbose, "Server::SendUpdate Client: {} Slot: {} Coord: ({},{}) Tick: {}", rClient.iClientId, iSlot, coord.x, coord.y, iTick);
			rClient.coordSubscriptions.at(iSlot).bFirstUpdateLogged = true;
		}
	}
}

void Server::SendResends(ClientConnection& rClient, int64_t iTick)
{
	// Heap: workbuffer Push and ENet packet creation per resend frame
	ScopedSuppressAllocationTracking suppress;

	// Iterate per-slot: each active subscription has its own ACK state and coord ring buffer
	for (int64_t iSlot = 0; iSlot < std::ssize(rClient.coordSubscriptions); ++iSlot)
	{
		if (!rClient.coordSubscriptions.at(iSlot).bActive)
		{
			if (rClient.prevResendCounts.at(iSlot) != 0)
			{
				rClient.prevResendCounts.at(iSlot) = 0;
			}
			continue;
		}

		const AckState& rAckState = rClient.coordAckStates.at(iSlot);
		if (rAckState.iAckFloor < 0)
		{
			if (rClient.prevResendCounts.at(iSlot) != 0)
			{
				rClient.prevResendCounts.at(iSlot) = 0;
			}
			continue;
		}

		GridCoord coord = rClient.coordSubscriptions.at(iSlot).coord;

		int64_t iAckGap = iTick - rAckState.iAckFloor;
		if (iAckGap > 64)
		{
			LOG(kNetwork, kVerbose, "Server::SendResends Client: {} Slot: {} Coord: ({},{}) AckFloor: {} CurrentTick: {} Gap: {}", rClient.iClientId, iSlot, coord.x, coord.y, rAckState.iAckFloor, iTick, iAckGap);
		}

		if (rAckState.uiReceivedBitfieldLow == 0 && rAckState.uiReceivedBitfieldHigh == 0)
		{
			if (rClient.prevResendCounts.at(iSlot) != 0)
			{
				rClient.prevResendCounts.at(iSlot) = 0;
			}
			continue;
		}
		int64_t iScanLimit = (rAckState.uiReceivedBitfieldHigh != 0)
			? std::max(static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldLow)), 64 + static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldHigh)))
			: static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldLow));

		int64_t aiResendTicks[kiMaxResendFrames] {};
		int64_t iSlotResendCount = 0;
		for (int64_t iBit = 0; iBit < iScanLimit && iSlotResendCount < kiMaxResendFrames; ++iBit)
		{
			bool bReceived = (iBit < 64) ? (rAckState.uiReceivedBitfieldLow & (1ULL << iBit)) != 0 : (rAckState.uiReceivedBitfieldHigh & (1ULL << (iBit - 64))) != 0;
			if (bReceived)
			{
				continue;
			}

			int64_t iMissingFrame = rAckState.iAckFloor + 1 + iBit;
			if (iMissingFrame >= iTick)
			{
				break;
			}

			const PerCoordBufferedFrame* pBuffered = FindBufferedFrame(coord, iMissingFrame);
			if (pBuffered == nullptr)
			{
				LOG(kNetwork, kVerbose, "Server::SendResends Evicted frame Client: {} Slot: {} Coord: ({},{}) MissingTick: {} LatestBuffered: {}", rClient.iClientId, iSlot, coord.x, coord.y, iMissingFrame, miLatestBufferedTick);
				continue;
			}

			common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
			common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

			WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordResend, iSlot, rClient.coordAckStates.at(iSlot).uiEpoch, *pBuffered, rClient.iClientTimestampNs);

			NetworkManager::SendPacket(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), rWorkbuffer, 0);

			aiResendTicks[iSlotResendCount] = iMissingFrame;
			++iSlotResendCount;
		}

		UpdateResendLogState(rClient, iSlot, iSlotResendCount, coord);
	}
}

void Server::UpdateResendLogState(ClientConnection& rClient, int64_t iSlot, int64_t iSlotResendCount, GridCoord coord)
{
	constexpr int64_t kiResendLogCooldownTicks = 64;

	bool bWasResending = rClient.prevResendCounts.at(iSlot) > 0;
	bool bIsResending = iSlotResendCount > 0;

	if (rClient.resendLogCooldowns.at(iSlot) > 0)
	{
		--rClient.resendLogCooldowns.at(iSlot);
	}

	if (bWasResending != bIsResending && rClient.resendLogCooldowns.at(iSlot) <= 0)
	{
		LOG(kNetwork, kVerbose, "Server::SendResends Client: {} Slot: {} Coord: ({},{}) Count: {}", rClient.iClientId, iSlot, coord.x, coord.y, iSlotResendCount);
		rClient.resendLogCooldowns.at(iSlot) = kiResendLogCooldownTicks;
	}
	rClient.prevResendCounts.at(iSlot) = iSlotResendCount;
}

void Server::BroadcastLoadNotification()
{
	LOG(kDefault, kDebug, "Server::BroadcastLoadNotification");

	for (ClientConnection& rClient : mClients)
	{
		if (!rClient.bHandshakeComplete)
		{
			continue;
		}

		SendSimplePacket(rClient.pPeer, PacketType::kServerLoadNotification, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE);
	}
}

} // namespace engine
