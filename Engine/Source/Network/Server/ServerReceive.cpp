#include "Pch.h"

#include "Network/Server/Server.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

#include "Game.h"

namespace engine
{

void Server::ClientAckStream(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// Read per-slot ACK state
	uint8_t uiAckSlotCount = ReadUint8(pCursor);

	// 1B type + 1B count + (27B per slot: 1B slot + 2B epoch + 8B floor + 8B bitfieldLow + 8B bitfieldHigh) + 8B timestamp
	size_t iExpectedSize = 2 + static_cast<size_t>(uiAckSlotCount) * 27 + 8;
	if (iSize < iExpectedSize)
	{
		return;
	}
	int64_t iFloorAdvanceCount = 0;
	for (uint8_t i = 0; i < uiAckSlotCount; ++i)
	{
		uint8_t uiSlotIndex = ReadUint8(pCursor);
		uint16_t uiSlotEpoch = ReadUint16(pCursor);
		int64_t iSlotAckFloor = ReadInt64(pCursor);
		uint64_t uiSlotBitfieldLow = ReadUint64(pCursor);
		uint64_t uiSlotBitfieldHigh = ReadUint64(pCursor);

		if (uiSlotIndex < std::ssize(pClient->coordSubscriptions) &&
			pClient->coordSubscriptions.at(uiSlotIndex).bActive &&
			uiSlotEpoch == pClient->coordAckStates.at(uiSlotIndex).uiEpoch &&
			iSlotAckFloor >= pClient->coordAckStates.at(uiSlotIndex).iAckFloor)
		{
			// Clamp to server's latest sent tick to prevent future ACK floors
			iSlotAckFloor = std::min(iSlotAckFloor, miLatestBufferedTick);
			AckState& rAckState = pClient->coordAckStates.at(uiSlotIndex);
			if (iSlotAckFloor == rAckState.iAckFloor)
			{
				rAckState.uiReceivedBitfieldLow |= uiSlotBitfieldLow;
				rAckState.uiReceivedBitfieldHigh |= uiSlotBitfieldHigh;
			}
			else
			{
				++iFloorAdvanceCount;
				rAckState.iAckFloor = iSlotAckFloor;
				rAckState.uiReceivedBitfieldLow = uiSlotBitfieldLow;
				rAckState.uiReceivedBitfieldHigh = uiSlotBitfieldHigh;
			}
		}
		else if (uiSlotIndex < std::ssize(pClient->coordSubscriptions) &&
			pClient->coordSubscriptions.at(uiSlotIndex).bActive &&
			uiSlotEpoch != pClient->coordAckStates.at(uiSlotIndex).uiEpoch)
		{
			LOG(kNetwork, kVerbose, "Server::ClientAckStream EpochMismatch Client: {} Slot: {} ClientEpoch: {} ServerEpoch: {}", iClientId, uiSlotIndex, uiSlotEpoch, pClient->coordAckStates.at(uiSlotIndex).uiEpoch);
		}
	}

	if (iFloorAdvanceCount > 0)
	{
		if (pClient->bFloorStalled)
		{
			if (pClient->iPeakConsecutiveStallAcks >= kiFloorStallLogThreshold)
			{
				LOG(kNetwork, kVerbose, "Server::ClientAckStream FloorStallResolved Client: {} PeakStalledAcks: {} Slots: {}",
					iClientId, pClient->iPeakConsecutiveStallAcks, uiAckSlotCount);
			}
			pClient->bFloorStalled = false;
			pClient->iPeakConsecutiveStallAcks = 0;
		}
		pClient->iConsecutiveZeroAdvanceAcks = 0;
	}
	else if (uiAckSlotCount > 0)
	{
		++pClient->iConsecutiveZeroAdvanceAcks;
		pClient->iPeakConsecutiveStallAcks = std::max(pClient->iPeakConsecutiveStallAcks, pClient->iConsecutiveZeroAdvanceAcks);
		if (pClient->iConsecutiveZeroAdvanceAcks >= 3)
		{
			pClient->bFloorStalled = true;
		}
	}

	// Pipeline RTT: store client timestamp for echo in SendUpdate (monotonically increasing to guard against out-of-order packets)
	int64_t iClientTimestampNs = ReadInt64(pCursor);
	if (iClientTimestampNs > pClient->iClientTimestampNs)
	{
		pClient->iClientTimestampNs = iClientTimestampNs;
	}
}

void Server::ClientSpawnRequest(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// 1B type + 1B flags = 2 fixed bytes
	if (iSize < 2)
	{
		return;
	}

	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiFlags = ReadUint8(pCursor);

	ClientRequestFlags_t flags;
	std::memcpy(&flags, &uiFlags, sizeof(uint8_t));

	LOG(kNetwork, kDebug, "Server::ClientSpawnRequest Client: {} Spawn: {} Respawn: {}", iClientId, static_cast<bool>(flags & ClientRequestFlags::kSpawnRequested), static_cast<bool>(flags & ClientRequestFlags::kRespawnRequested));

	ScopedSuppressAllocationTracking suppress;
	// Heap: spawn request vector grows on request
	mPendingSpawnRequests.push_back({iClientId, flags});
}

void Server::ClientDesyncReport(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// 1B type + 8B tick + 4B gridX + 4B gridY + 8B expectedCrc + 8B actualCrc = 33 fixed bytes
	if (iSize < 33)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	uint64_t uiExpectedCrc = ReadUint64(pCursor);
	uint64_t uiActualCrc = ReadUint64(pCursor);

	char pcExpected[20] {};
	char pcActual[20] {};
	LOG(kNetwork, kError, "Server::ClientDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), uiExpectedCrc), common::ToHex(std::span(pcActual), uiActualCrc));
}

void Server::ClientDebugFrameRequest(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// 1B type + 8B tick + 4B gridX + 4B gridY = 17 fixed bytes
	if (iSize < 17)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	LOG(kNetwork, kError, "Server::ClientDebugFrameRequest Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

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
		LOG(kNetwork, kError, "Server::ClientDebugFrameRequest Frame {} not found in buffer", iTick);
		return;
	}

	auto it = pBuffered->serializedFrames.find(coord);
	if (it == pBuffered->serializedFrames.end())
	{
		LOG(kNetwork, kError, "Server::ClientDebugFrameRequest Frame: {} Coord: ({},{}) not found", iTick, coord.x, coord.y);
		return;
	}

	const std::string& rFrameData = it->second;

	// Heap: compression buffer may grow when LZ4 expansion bound exceeds current capacity
	ScopedSuppressAllocationTracking suppress;

	// LZ4 compress (reuse persistent compression buffer)
	int iCompressedSize = CompressToBuffer(rFrameData.data(), static_cast<int>(rFrameData.size()));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// [1B type][8B frame][4B gridX][4B gridY][4B uncompressedSize][4B compressedSize][...LZ4 data]
	rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerDebugFrame));
	rWorkbuffer.PushBack<int64_t>(iTick);
	WriteGridCoord(rWorkbuffer, coord);
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(rFrameData.size()));
	rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(iCompressedSize));
	rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(mCompressionBuffer.data()), iCompressedSize));

	NetworkManager::SendPacket(pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::ClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId)
{
	// 1B type + 4B protocolVersion + 8B frameVersion = 13 minimum bytes
	if (iSize < 13)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint32_t uiClientProtocolVersion = ReadUint32(pCursor);
	if (uiClientProtocolVersion != kuiProtocolVersion)
	{
		char pcMessage[256] {};
		snprintf(pcMessage, sizeof(pcMessage), "Protocol version mismatch: server is %u, client is %u", kuiProtocolVersion, uiClientProtocolVersion);
		LOG(kNetwork, kWarning, "Server::ClientHello Rejecting Client: {} Reason: {}", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage, nullptr);
		RemoveClient(iClientId);
		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	int64_t iClientFrameVersion = ReadInt64(pCursor);
	if (iClientFrameVersion != game::Frame::kiVersion)
	{
		char pcMessage[256] {};
		snprintf(pcMessage, sizeof(pcMessage), "Frame version mismatch: server is %lld, client is %lld", game::Frame::kiVersion, iClientFrameVersion);
		LOG(kNetwork, kWarning, "Server::ClientHello Rejecting Client: {} Reason: {}", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage, nullptr);
		RemoveClient(iClientId);
		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	// Read build config string (null-terminated, variable length)
	size_t iConfigOffset = static_cast<size_t>(pCursor - pData);
	size_t iConfigMaxLength = 0;
	for (size_t i = iConfigOffset; i < iSize; ++i)
	{
		++iConfigMaxLength;
		if (pData[i] == '\0')
		{
			break;
		}
	}
	char pcClientConfig[64] = {};
	size_t iCopyLength = std::min(iConfigMaxLength, sizeof(pcClientConfig) - 1);
	std::memcpy(pcClientConfig, pCursor, iCopyLength);
	pCursor += iConfigMaxLength;

	if (strcmp(pcClientConfig, kpcBuildConfigName) != 0)
	{
		LOG(kNetwork, kWarning, "Server::ClientHello Client {} build config mismatch: server is {}, client is {}", iClientId, kpcBuildConfigName, pcClientConfig);
	}

	// Read client GUID (16 bytes after config string)
	ClientGuid clientGuid {};
	size_t iGuidOffset = static_cast<size_t>(pCursor - pData);
	if (iGuidOffset + 16 <= iSize)
	{
		clientGuid.uiHigh = ReadUint64(pCursor);
		clientGuid.uiLow = ReadUint64(pCursor);
	}

	// Generate GUID if client sent empty
	if (clientGuid.IsEmpty())
	{
		::UUID uuid;
		[[maybe_unused]] RPC_STATUS rpcStatus = UuidCreate(&uuid); // RPC_S_UUID_LOCAL_ONLY still yields a usable UUID
		std::memcpy(&clientGuid.uiHigh, &uuid, 8);
		std::memcpy(&clientGuid.uiLow, reinterpret_cast<const uint8_t*>(&uuid) + 8, 8);
	}

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient != nullptr)
	{
		pClient->bHandshakeComplete = true;
		pClient->clientGuid = clientGuid;
	}

	LOG(kNetwork, kInfo, "Server::ClientHello Accepted Client: {} Config: {} GUID: {} {}", iClientId, pcClientConfig, clientGuid.uiHigh, clientGuid.uiLow);
	SendConnectionResponse(pPeer, true, nullptr, &clientGuid);

	// Send current timespeed so clients joining a non-1x server stay in sync
	if (game::gpGame->mTimeStep.miTimeMultiply != 1 || game::gpGame->mTimeStep.miTimeDivide != 1)
	{
		SendTimespeedUpdate(pPeer, game::gpGame->mTimeStep.miTimeMultiply, game::gpGame->mTimeStep.miTimeDivide);
	}
}

void Server::ClientSubscribe(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// 1B type + 4B gridX + 4B gridY = 9 fixed bytes
	if (iSize < 9)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	GridCoord coord = ReadGridCoord(pCursor);

	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	// Already subscribed?
	if (pClient->IsCoordSubscribed(coord))
	{
		LOG(kNetwork, kVerbose, "Server::ClientSubscribe AlreadySubscribed Client: {} Coord: ({},{})", iClientId, coord.x, coord.y);
		return;
	}

	// Validate coord is adjacent to ANY owned player's coord (3x3 grid)
	// Origin coord is always allowed (always simulated, needed for initial fleet spawn)
	bool bAdjacent = (coord == kOriginCoord);
	for (const GridCoord& rOwnedCoord : pClient->authorizedCoords)
	{
		int32_t iDeltaX = std::abs(coord.x - rOwnedCoord.x);
		int32_t iDeltaY = std::abs(coord.y - rOwnedCoord.y);
		if (iDeltaX <= 1 && iDeltaY <= 1)
		{
			bAdjacent = true;
			break;
		}
	}
	if (!bAdjacent)
	{
		LOG(kNetwork, kWarning, "Server::ClientSubscribe Rejected (not adjacent) Client: {} Coord: ({},{})", iClientId, coord.x, coord.y);
		SendSubscribeAccept(*pClient, 0xFF, coord);
		return;
	}

	int64_t iSlot = pClient->AllocateSlot();
	if (iSlot < 0)
	{
		LOG(kNetwork, kWarning, "Server::ClientSubscribe No free slot Client: {} Coord: ({},{})", iClientId, coord.x, coord.y);
		SendSubscribeAccept(*pClient, 0xFF, coord);
		return;
	}

	pClient->coordSubscriptions.at(iSlot).coord = coord;
	pClient->coordSubscriptions.at(iSlot).bActive = true;
	++pClient->coordAckStates.at(iSlot).uiEpoch;

	LOG(kNetwork, kDebug, "Server::ClientSubscribe Client: {} Coord: ({},{}) Slot: {}", iClientId, coord.x, coord.y, iSlot);

	SendSubscribeAccept(*pClient, iSlot, coord);

	ScopedSuppressAllocationTracking suppress;
	// Heap: pending subscription entry
	mPendingNewSubscriptions.push_back({iClientId, iSlot, coord});
}

void Server::ClientUnsubscribe(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// 1B type + 1B slot = 2 fixed bytes
	if (iSize < 2)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	if (uiSlotIndex >= std::ssize(pClient->coordSubscriptions) || !pClient->coordSubscriptions.at(uiSlotIndex).bActive)
	{
		return;
	}

	GridCoord coord = pClient->coordSubscriptions.at(uiSlotIndex).coord;
	pClient->FreeSlot(uiSlotIndex);

	LOG(kNetwork, kDebug, "Server::ClientUnsubscribe Client: {} Slot: {} Coord: ({},{})", iClientId, uiSlotIndex, coord.x, coord.y);

	SendSimplePacket(pClient->pPeer, PacketType::kServerUnsubscribeAck, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, uiSlotIndex);
}

void Server::ClientResyncRequest([[maybe_unused]] const uint8_t* pData, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kError, "Server::ClientResyncRequest Client: {}", iClientId);

	// Heap: pending resync client-id vector grows on request
	ScopedSuppressAllocationTracking suppress;
	mPendingResyncClientIds.push_back(iClientId);
}

void Server::ClientPauseRequest(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// [1B type][1B paused]
	if (iSize < 2)
	{
		return;
	}

	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiPaused = ReadUint8(pCursor);

	game::gpGame->mGameFlags.Set(GameFlags::kPaused, uiPaused != 0);
	LOG(kDefault, kDebug, "Server paused: {}", uiPaused != 0);
}

void Server::ClientTimespeedRequest(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// [1B type][1B direction]
	if (iSize < 2)
	{
		return;
	}

	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type
	uint8_t uiDirection = ReadUint8(pCursor);

	if (uiDirection == 0)
	{
		game::gpGame->mTimeStep.DecreaseTimeScale();
	}
	else
	{
		game::gpGame->mTimeStep.IncreaseTimeScale();
	}

	BroadcastTimespeedUpdate(game::gpGame->mTimeStep.miTimeMultiply, game::gpGame->mTimeStep.miTimeDivide);
}

#if defined(BT_SERVER)
void Server::ClientSaveRequest([[maybe_unused]] const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kDefault, kDebug, "Server::ClientSaveRequest Client: {}", iClientId);
	game::gpGame->mGameSaveLoad.ServerSave();
}

void Server::ClientLoadRequest([[maybe_unused]] const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kDefault, kDebug, "Server::ClientLoadRequest Client: {}", iClientId);
	game::gpGame->mGameSaveLoad.ServerLoad();
}

void Server::ClientReplayRecordRequest([[maybe_unused]] const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kDefault, kDebug, "Server::ClientReplayRecordRequest Client: {}", iClientId);
	game::gpGame->mGameFlags.Set(GameFlags::kSaveReplay);
}

void Server::ClientReplayPlaybackRequest([[maybe_unused]] const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kDefault, kDebug, "Server::ClientReplayPlaybackRequest Client: {}", iClientId);
	game::gpGame->mGameFlags.Set(GameFlags::kLoadReplay);
}

void Server::ClientResetRequest([[maybe_unused]] const uint8_t* pData, [[maybe_unused]] size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kDefault, kDebug, "Server::ClientResetRequest Client: {}", iClientId);
	game::gpGame->mGameSaveLoad.ServerReset();
}
#endif // BT_SERVER

} // namespace engine
