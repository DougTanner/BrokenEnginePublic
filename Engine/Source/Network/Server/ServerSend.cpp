#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/Server/Server.h"

#include "Frame/FrameStaticData.h"
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

	// Serialize frame into the reusable scratch (server main thread only - single-writer contract)
	ScopedSuppressAllocationTracking suppress;
	// Heap: scratch grows until steady state
	mSendScratch.clear();
	mFrameStreamBuf.mpTarget = &mSendScratch;
	game::NetworkSessionContract::WriteFrame(mFrameStream, *pFrame);

	// LZ4 compress
	int iCompressedSize = CompressToBuffer(mSendScratch.data(), static_cast<int>(mSendScratch.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	NetworkMessages::ServerCoordFullStateMessage message {
		.uiSlotIndex = static_cast<uint8_t>(iSlot),
		.uiEpoch = pClient->slots.at(iSlot).ack.uiEpoch,
		.iTick = iTick,
		.coord = coord,
		.iUncompressedSize = static_cast<int32_t>(mSendScratch.size()),
		.compressedPayload = {.pData = mCompressionBuffer.data(), .iSize = static_cast<int32_t>(iCompressedSize)},
	};
	NetworkMessages::Write(rWorkbuffer, message);

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

	// Serialize static data into the reusable scratch (server main thread only - single-writer contract)
	ScopedSuppressAllocationTracking suppress;
	// Heap: scratch grows until steady state
	mSendScratch.clear();
	mFrameStreamBuf.mpTarget = &mSendScratch;
	rStaticData.Write(mFrameStream, /*bIncludeNavData=*/true);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	NetworkMessages::ServerCoordStaticDataMessage message {
		.uiSlotIndex = static_cast<uint8_t>(iSlot),
		.uiEpoch = pClient->slots.at(iSlot).ack.uiEpoch,
		.coord = coord,
		.staticData = {.pData = reinterpret_cast<const uint8_t*>(mSendScratch.data()), .iSize = static_cast<int32_t>(mSendScratch.size())},
	};
	NetworkMessages::Write(rWorkbuffer, message);

	NetworkManager::SendPacket(pClient->pPeer, NetworkManager::CoordSlotReliable(iSlot), rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::SendConnectionResponse(ENetPeer* pPeer, bool bAccepted, const char* pMessage, const ClientGuid* pGuid)
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	NetworkMessages::ServerConnectionResponseMessage message {
		.uiAccepted = bAccepted ? 1u : 0u,
		.guid = (pGuid != nullptr) ? *pGuid : ClientGuid {},
		.bHasGuid = bAccepted && pGuid != nullptr,
		.rejectionMessage = (!bAccepted && pMessage != nullptr) ? pMessage : "",
	};
	NetworkMessages::Write(rWorkbuffer, message);

	NetworkManager::SendPacket(pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::SendSubscribeAccept(ClientConnection& rClient, int64_t iSlot, GridCoord coord)
{
	uint16_t uiEpoch = (iSlot < std::ssize(rClient.slots)) ? rClient.slots.at(iSlot).ack.uiEpoch : 0;
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ServerSubscribeAcceptMessage message {
		.uiSlotIndex = static_cast<uint8_t>(iSlot),
		.uiEpoch = uiEpoch,
		.coord = coord,
	};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(rClient.pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::WriteBufferedFramePacket(common::Workbuffer& rWorkbuffer, PacketType eType, int64_t iSlot, uint16_t uiEpoch, const PerCoordBufferedFrame& rBuffered, int64_t iTimestampNs)
{
	NetworkMessages::CoordUpdateFields fields {
		.uiSlotIndex = static_cast<uint8_t>(iSlot),
		.uiEpoch = uiEpoch,
		.iTick = rBuffered.iTick,
		.iEchoedTimestampNs = iTimestampNs,
		.uiSharedCrc = rBuffered.sharedCrc,
		.compressedPayload = {.pData = rBuffered.compressedData.data(), .iSize = static_cast<int32_t>(rBuffered.compressedData.size())},
	};
	if (eType == PacketType::kServerCoordUpdate)
	{
		NetworkMessages::ServerCoordUpdateMessage message {};
		static_cast<NetworkMessages::CoordUpdateFields&>(message) = fields;
		NetworkMessages::Write(rWorkbuffer, message);
	}
	else if (eType == PacketType::kServerCoordResend)
	{
		NetworkMessages::ServerCoordResendMessage message {};
		static_cast<NetworkMessages::CoordUpdateFields&>(message) = fields;
		NetworkMessages::Write(rWorkbuffer, message);
	}
}

void Server::SendUpdate(ClientConnection& rClient, int64_t iTick)
{
	// Heap: workbuffer Push (per-slot scope) and ENet packet creation in SendPacket
	ScopedSuppressAllocationTracking suppress;

	// Send one packet per active subscription slot
	for (int64_t iSlot = 0; iSlot < std::ssize(rClient.slots); ++iSlot)
	{
		if (!(rClient.slots.at(iSlot).subscription.flags & SubscriptionFlags::kActive))
		{
			continue;
		}

		GridCoord coord = rClient.slots.at(iSlot).subscription.coord;

		const PerCoordBufferedFrame* pBuffered = FindBufferedFrame(coord, iTick);
		if (pBuffered == nullptr)
		{
			continue;
		}

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

		WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordUpdate, iSlot, rClient.slots.at(iSlot).ack.uiEpoch, *pBuffered, rClient.iClientTimestampNs);

		NetworkManager::SendPacket(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), rWorkbuffer, 0);
		if (!(rClient.slots.at(iSlot).subscription.flags & SubscriptionFlags::kFirstUpdateLogged))
		{
			LOG(kNetwork, kVerbose, "Server::SendUpdate Client: {} Slot: {} Coord: ({},{}) Tick: {}", rClient.iClientId, iSlot, coord.x, coord.y, iTick);
			rClient.slots.at(iSlot).subscription.flags.Set(SubscriptionFlags::kFirstUpdateLogged);
		}
	}
}

void Server::SendResends(ClientConnection& rClient, int64_t iTick)
{
	// Heap: workbuffer Push and ENet packet creation per resend frame
	ScopedSuppressAllocationTracking suppress;

	// Iterate per-slot: each active subscription has its own ACK state and coord ring buffer
	for (int64_t iSlot = 0; iSlot < std::ssize(rClient.slots); ++iSlot)
	{
		GridCoord coord = rClient.slots.at(iSlot).subscription.coord;

		if (!(rClient.slots.at(iSlot).subscription.flags & SubscriptionFlags::kActive))
		{
			UpdateResendLogState(rClient, iSlot, 0, coord);
			continue;
		}

		const AckState& rAckState = rClient.slots.at(iSlot).ack;
		if (rAckState.iAckFloor < 0)
		{
			UpdateResendLogState(rClient, iSlot, 0, coord);
			continue;
		}

		int64_t iAckGap = iTick - rAckState.iAckFloor;
		if (iAckGap > 64)
		{
			LOG(kNetwork, kVerbose, "Server::SendResends Client: {} Slot: {} Coord: ({},{}) AckFloor: {} CurrentTick: {} Gap: {}", rClient.iClientId, iSlot, coord.x, coord.y, rAckState.iAckFloor, iTick, iAckGap);
		}

		if (rAckState.uiReceivedBitfieldLow == 0 && rAckState.uiReceivedBitfieldHigh == 0)
		{
			UpdateResendLogState(rClient, iSlot, 0, coord);
			continue;
		}
		int64_t iScanLimit = (rAckState.uiReceivedBitfieldHigh != 0)
			? std::max(static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldLow)), 64 + static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldHigh)))
			: static_cast<int64_t>(std::bit_width(rAckState.uiReceivedBitfieldLow));

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

			WriteBufferedFramePacket(rWorkbuffer, PacketType::kServerCoordResend, iSlot, rClient.slots.at(iSlot).ack.uiEpoch, *pBuffered, rClient.iClientTimestampNs);

			NetworkManager::SendPacket(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), rWorkbuffer, 0);

			++iSlotResendCount;
		}

		UpdateResendLogState(rClient, iSlot, iSlotResendCount, coord);
	}
}

void Server::UpdateResendLogState(ClientConnection& rClient, int64_t iSlot, int64_t iSlotResendCount, GridCoord coord)
{
	constexpr int64_t kiResendLogCooldownTicks = 64;

	bool bWasResending = rClient.slots.at(iSlot).iPrevResendCount > 0;
	bool bIsResending = iSlotResendCount > 0;

	if (rClient.slots.at(iSlot).iResendLogCooldown > 0)
	{
		--rClient.slots.at(iSlot).iResendLogCooldown;
	}

	if (bWasResending != bIsResending && rClient.slots.at(iSlot).iResendLogCooldown <= 0)
	{
		LOG(kNetwork, kVerbose, "Server::SendResends Client: {} Slot: {} Coord: ({},{}) Count: {}", rClient.iClientId, iSlot, coord.x, coord.y, iSlotResendCount);
		rClient.slots.at(iSlot).iResendLogCooldown = kiResendLogCooldownTicks;
	}
	rClient.slots.at(iSlot).iPrevResendCount = iSlotResendCount;
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

		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		NetworkMessages::ServerLoadNotificationMessage message {};
		NetworkMessages::Write(rWorkbuffer, message);
		NetworkManager::SendPacket(rClient.pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	}
}

} // namespace engine

#endif // BT_SERVER
