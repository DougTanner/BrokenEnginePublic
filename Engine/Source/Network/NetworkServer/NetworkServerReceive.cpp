#include "Pch.h"

#include "Network/NetworkServer/NetworkServer.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

void NetworkServer::HandleClientAckStream(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	ClientConnection* pClient = FindClient(iClientId);

	// Read per-slot ACK state
	uint8_t uiAckSlotCount = ReadUint8(pCursor);
	if (pClient != nullptr)
	{
		for (uint8_t i = 0; i < uiAckSlotCount; ++i)
		{
			uint8_t uiSlotIndex = ReadUint8(pCursor);
			uint16_t uiSlotEpoch = ReadUint16(pCursor);
			int64_t iSlotAckFloor = ReadInt64(pCursor);
			uint64_t uiSlotBitfield = ReadUint64(pCursor);

			if (uiSlotIndex < std::ssize(pClient->coordSubscriptions)
			&& pClient->coordSubscriptions[uiSlotIndex].bActive
			&& uiSlotEpoch == pClient->coordAckStates[uiSlotIndex].uiEpoch
			&& iSlotAckFloor >= pClient->coordAckStates[uiSlotIndex].iAckFloor)
			{
				if (iSlotAckFloor == pClient->coordAckStates[uiSlotIndex].iAckFloor)
				{
					pClient->coordAckStates[uiSlotIndex].uiReceivedBitfield |= uiSlotBitfield;
				}
				else
				{
					pClient->coordAckStates[uiSlotIndex].iAckFloor = iSlotAckFloor;
					pClient->coordAckStates[uiSlotIndex].uiReceivedBitfield = uiSlotBitfield;
				}
			}
		}
	}
	else
	{
		// Skip ACK data if client not found
		for (uint8_t i = 0; i < uiAckSlotCount; ++i)
		{
			pCursor += 1 + 2 + 8 + 8; // slotIndex + epoch + ackFloor + bitfield
		}
	}

	// Pipeline RTT: store client timestamp for echo in SendUpdate (monotonically increasing to guard against out-of-order packets)
	int64_t iClientTimestampNs = ReadInt64(pCursor);
	if (pClient != nullptr && iClientTimestampNs > pClient->iClientTimestampNs)
	{
		pClient->iClientTimestampNs = iClientTimestampNs;
	}
}

void NetworkServer::HandleClientSpawnRequest(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiFlags = ReadUint8(pCursor);

	ClientRequestFlags_t flags;
	std::memcpy(&flags, &uiFlags, sizeof(uint8_t));

	common::Log("NetworkServer: Client {} spawn request (spawn={}, respawn={})", iClientId, static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: spawn request vector grows on request
	mPendingSpawnRequests.push_back({iClientId, flags});
}

void NetworkServer::HandleClientDesyncReport(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	uint64_t uiExpectedCrc = ReadUint64(pCursor);
	uint64_t uiActualCrc = ReadUint64(pCursor);

	char pcExpected[20] {};
	char pcActual[20] {};
	common::Log("NetworkServer: Desync report frame {} grid ({},{}) expected={} actual={}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), uiExpectedCrc), common::ToHex(std::span(pcActual), uiActualCrc));
}

void NetworkServer::HandleClientDebugFrameRequest(const uint8_t* pData, ENetPeer* pPeer)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	common::Log("NetworkServer: Debug frame request frame {} grid ({},{})", iTick, coord.x, coord.y);

	// Find the frame in the ring buffer
	const BufferedFullFrame* pBuffered = nullptr;
	for (const BufferedFullFrame& rBuffered : mBufferedFullFrames)
	{
		if (rBuffered.iTick == iTick)
		{
			pBuffered = &rBuffered;
			break;
		}
	}

	if (pBuffered == nullptr)
	{
		common::Log("NetworkServer: Debug frame {} not found in buffer", iTick);
		return;
	}

	auto it = pBuffered->serializedFrames.find(coord);
	if (it == pBuffered->serializedFrames.end())
	{
		common::Log("NetworkServer: Debug frame {} coord ({},{}) not found", iTick, coord.x, coord.y);
		return;
	}

	const std::string& rFrameData = it->second;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	// LZ4 compress (reuse persistent compression buffer)
	int iCompressedSize = CompressToBuffer(rFrameData.data(), static_cast<int>(rFrameData.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	rWorkbuffer.Push();

	// [1B type][8B frame][4B gridX][4B gridY][4B uncompressedSize][4B compressedSize][...LZ4 data]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerDebugFrame));
	rWorkbuffer.PushBack<int64_t>(iTick);
	rWorkbuffer.PushBack<int32_t>(coord.x);
	rWorkbuffer.PushBack<int32_t>(coord.y);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(rFrameData.size()));
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
	rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(mCompressionBuffer.data()), iCompressedSize));

	std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

	// Heap: ENet allocates packet data internally
	ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), ENET_PACKET_FLAG_RELIABLE);
	enet_peer_send(pPeer, NetworkManager::kuiChannelReliable, pPacket);

	rWorkbuffer.Pop();
}

void NetworkServer::HandleClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId)
{
	char pcClientConfig[64] = {};
	size_t iLength = std::min(iSize - 1, sizeof(pcClientConfig) - 1);
	std::memcpy(pcClientConfig, pData + 1, iLength);

	if (strcmp(pcClientConfig, kpcBuildConfigName) != 0)
	{
		char pcMessage[256] {};
		snprintf(pcMessage, sizeof(pcMessage), "Build mismatch: server is %s, client is %s", kpcBuildConfigName, pcClientConfig);
		common::Log("NetworkServer: Rejecting client {} ({})", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage);

		// Remove from mClients (added during HandleConnect before hello arrived)
		RemoveClient(iClientId);

		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	common::Log("NetworkServer: Client {} hello accepted (config: {})", iClientId, pcClientConfig);
	SendConnectionResponse(pPeer, true, nullptr);
}

void NetworkServer::HandleClientSubscribe(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	GridCoord coord = ReadGridCoord(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// Already subscribed?
	if (pClient->IsCoordSubscribed(coord))
	{
		return;
	}

	int64_t iSlot = pClient->AllocateSlot();
	if (iSlot < 0)
	{
		common::Log("NetworkServer: No free coord slot for client {} subscribing to ({},{})", iClientId, coord.x, coord.y);
		SendSubscribeAccept(*pClient, 0xFF, coord);
		return;
	}

	pClient->coordSubscriptions[iSlot].coord = coord;
	pClient->coordSubscriptions[iSlot].bActive = true;
	++pClient->coordAckStates[iSlot].uiEpoch;

	common::Log("NetworkServer: Client {} subscribed to ({},{}) slot {}", iClientId, coord.x, coord.y, iSlot);
	FILE_LOG(0, "[HandleClientSubscribe] client={} coord=({},{}) slot={}", iClientId, coord.x, coord.y, iSlot);

	SendSubscribeAccept(*pClient, iSlot, coord);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	// Heap: pending subscription entry
	mPendingNewSubscriptions.push_back({iClientId, iSlot, coord});
}

void NetworkServer::HandleClientUnsubscribe(const uint8_t* pData, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	if (uiSlotIndex >= std::ssize(pClient->coordSubscriptions) || !pClient->coordSubscriptions[uiSlotIndex].bActive)
	{
		return;
	}

	GridCoord coord = pClient->coordSubscriptions[uiSlotIndex].coord;
	pClient->FreeSlot(uiSlotIndex);

	common::Log("NetworkServer: Client {} unsubscribed slot {} coord ({},{})", iClientId, uiSlotIndex, coord.x, coord.y);
	FILE_LOG(0, "[HandleClientUnsubscribe] client={} slot={} coord=({},{})", iClientId, uiSlotIndex, coord.x, coord.y);

	SendUnsubscribeAck(*pClient, uiSlotIndex);
}

} // namespace engine
