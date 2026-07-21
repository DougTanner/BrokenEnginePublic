#include "Pch.h"

#if defined(BT_SERVER)

#include "Network/Server/Server.h"
#include "Network/Server/ServerSessionRuntime.h"

#include "Network/NetworkCursor.h"

#include "Game.h"

namespace engine
{

namespace
{

constexpr std::chrono::seconds kDesyncDiagnosticCooldown = 2s;

} // namespace

void Server::ClientAckStream(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	BoundedCursor cursor {pData + 1, pData + iSize}; // Skip packet type
	const uint8_t*& pCursor = cursor.pCursor;

	// 1B count byte must be present
	if (!cursor.Has(1))
	{
		return;
	}
	uint8_t uiAckSlotCount = ReadUint8(pCursor);

	// Exact-size check the dispatch table cannot express (the table only bounds min/max): 1B type + 1B count +
	// count*27B per slot (1B slot + 2B epoch + 8B floor + 8B bitfieldLow + 8B bitfieldHigh) + 8B timestamp.
	if (static_cast<int64_t>(iSize) != 2 + static_cast<int64_t>(uiAckSlotCount) * 27 + 8)
	{
		RecordContractViolation(iClientId, "ackstream size", pData[0], static_cast<int64_t>(iSize));
		return;
	}

	int64_t iFloorAdvanceCount = 0;
	for (uint8_t i = 0; i < uiAckSlotCount; ++i)
	{
		// 27B per slot guaranteed present by the exact-size check; Has() keeps the bounded contract explicit.
		if (!cursor.Has(27))
		{
			break;
		}
		uint8_t uiSlotIndex = ReadUint8(pCursor);
		uint16_t uiSlotEpoch = ReadUint16(pCursor);
		int64_t iSlotAckFloor = ReadInt64(pCursor);
		uint64_t uiSlotBitfieldLow = ReadUint64(pCursor);
		uint64_t uiSlotBitfieldHigh = ReadUint64(pCursor);

		// Invalid-index or inactive slots match neither branch below; skip after the reads so the cursor stays aligned
		if (!(uiSlotIndex < std::ssize(pClient->coordSubscriptions) &&
			(pClient->coordSubscriptions.at(uiSlotIndex).flags & SubscriptionFlags::kActive)))
		{
			continue;
		}

		if (uiSlotEpoch == pClient->coordAckStates.at(uiSlotIndex).uiEpoch &&
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
		else if (uiSlotEpoch != pClient->coordAckStates.at(uiSlotIndex).uiEpoch)
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

	// 8B timestamp guaranteed present by the exact-size check; Has() keeps the bounded contract explicit.
	if (!cursor.Has(8))
	{
		return;
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

	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now < pClient->desyncReportDeadline)
	{
		return;
	}
	pClient->desyncReportDeadline = now + kDesyncDiagnosticCooldown;

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

	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now < pClient->debugFrameRequestDeadline)
	{
		return;
	}
	pClient->debugFrameRequestDeadline = now + kDesyncDiagnosticCooldown;

	if constexpr (game::NetworkSessionContract::kbDebugFrames)
	{
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
}

void Server::ClientHello(const uint8_t* pData, size_t iSize, ENetPeer* pPeer, int64_t iClientId)
{
	// 1B type + 4B protocolVersion + 8B frameVersion + 8B packIntegrityToken = 21 minimum bytes
	if (iSize < 21)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint32_t uiClientProtocolVersion = ReadUint32(pCursor);
	if (uiClientProtocolVersion != kuiProtocolVersion)
	{
		char pcMessage[256] {};
		std::snprintf(pcMessage, sizeof(pcMessage), "Protocol version mismatch: server is %u, client is %u", kuiProtocolVersion, uiClientProtocolVersion);
		LOG(kNetwork, kWarning, "Server::ClientHello Rejecting Client: {} Reason: {}", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage, nullptr);
		RemoveClient(iClientId);
		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	int64_t iClientFrameVersion = ReadInt64(pCursor);
	if (iClientFrameVersion != game::NetworkSessionContract::GetFrameVersion())
	{
		char pcMessage[256] {};
		std::snprintf(pcMessage, sizeof(pcMessage), "Frame version mismatch: server is %lld, client is %lld", game::NetworkSessionContract::GetFrameVersion(), iClientFrameVersion);
		LOG(kNetwork, kWarning, "Server::ClientHello Rejecting Client: {} Reason: {}", iClientId, pcMessage);

		SendConnectionResponse(pPeer, false, pcMessage, nullptr);
		RemoveClient(iClientId);
		enet_peer_disconnect_later(pPeer, 0);
		return;
	}

	common::crc_t clientPackIntegrityToken = ReadUint64(pCursor);
	common::crc_t serverPackIntegrityToken = gpFileManager->GetPackIntegrityToken();
	if (clientPackIntegrityToken != serverPackIntegrityToken)
	{
		char pcMessage[256] {};
		std::snprintf(pcMessage, sizeof(pcMessage), "Pack integrity mismatch: server token is %llu, client token is %llu. Regenerate generated game data and retry.", static_cast<unsigned long long>(serverPackIntegrityToken), static_cast<unsigned long long>(clientPackIntegrityToken));
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

	if (std::strcmp(pcClientConfig, kpcBuildConfigName) != 0)
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

	// Reject a Hello for a client with no server-side connection state (never accept a ghost).
	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		LOG(kNetwork, kWarning, "Server::ClientHello Rejecting Client: {} Reason: no connection state", iClientId);
		return;
	}

	// Idempotent replay: an already-handshaken client re-sends the accept using its STORED GUID.
	// Do not overwrite established identity (fleet ownership is GUID-keyed) or mint a fresh GUID.
	if (pClient->bHandshakeComplete)
	{
		LOG(kNetwork, kInfo, "Server::ClientHello Replay Client: {} GUID: {} {}", iClientId, pClient->clientGuid.uiHigh, pClient->clientGuid.uiLow);
		SendConnectionResponse(pPeer, true, nullptr, &pClient->clientGuid);
		mrSessionRuntime.mrSession.SendTimespeedToNewClient(pPeer);
		return;
	}

	// Generate GUID if client sent empty
	if (clientGuid.IsEmpty())
	{
		::UUID uuid;
		[[maybe_unused]] RPC_STATUS rpcStatus = UuidCreate(&uuid); // RPC_S_UUID_LOCAL_ONLY still yields a usable UUID
		std::memcpy(&clientGuid.uiHigh, &uuid, 8);
		std::memcpy(&clientGuid.uiLow, reinterpret_cast<const uint8_t*>(&uuid) + 8, 8);
	}

	pClient->bHandshakeComplete = true;
	pClient->clientGuid = clientGuid;

	LOG(kNetwork, kInfo, "Server::ClientHello Accepted Client: {} Config: {} GUID: {} {}", iClientId, pcClientConfig, clientGuid.uiHigh, clientGuid.uiLow);
	SendConnectionResponse(pPeer, true, nullptr, &clientGuid);

	mrSessionRuntime.mrSession.SendTimespeedToNewClient(pPeer);
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
	if (pClient->FindSlotForCoord(coord) >= 0)
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
		SendSubscribeAccept(*pClient, kuiSubscribeRejectSlot, coord);
		return;
	}

	int64_t iSlot = pClient->AllocateSlot(game::NetworkSessionContract::kiCoordSlots);
	if (iSlot < 0)
	{
		LOG(kNetwork, kWarning, "Server::ClientSubscribe No free slot Client: {} Coord: ({},{})", iClientId, coord.x, coord.y);
		SendSubscribeAccept(*pClient, kuiSubscribeRejectSlot, coord);
		return;
	}

	pClient->coordSubscriptions.at(iSlot).coord = coord;
	pClient->coordSubscriptions.at(iSlot).flags.Set(SubscriptionFlags::kActive);
	++pClient->coordAckStates.at(iSlot).uiEpoch;

	LOG(kNetwork, kDebug, "Server::ClientSubscribe Client: {} Coord: ({},{}) Slot: {}", iClientId, coord.x, coord.y, iSlot);

	SendSubscribeAccept(*pClient, iSlot, coord);

	ScopedSuppressAllocationTracking suppress;
	// Replace an existing pending entry for this client+slot (a subscribe->unsubscribe->subscribe
	// cycle reuses the slot with a new coord) rather than appending a duplicate.
	auto pendingIt = std::ranges::find_if(mPendingNewSubscriptions, [iClientId, iSlot](const PendingNewSubscription& rPending) { return rPending.iClientId == iClientId && rPending.iSlot == iSlot; });
	if (pendingIt != mPendingNewSubscriptions.end())
	{
		pendingIt->coord = coord;
	}
	else
	{
		// Heap: pending subscription entry
		mPendingNewSubscriptions.push_back({iClientId, iSlot, coord});
	}
}

void Server::ClientUnsubscribe(const uint8_t* pData, size_t iSize, int64_t iClientId)
{
	// [1B type][1B slot][2B epoch]
	if (iSize != 4)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	if (uiSlotIndex < std::ssize(pClient->coordSubscriptions)
		&& (pClient->coordSubscriptions.at(uiSlotIndex).flags & SubscriptionFlags::kActive)
		&& pClient->coordAckStates.at(uiSlotIndex).uiEpoch == uiEpoch)
	{
		GridCoord coord = pClient->coordSubscriptions.at(uiSlotIndex).coord;
		pClient->FreeSlot(uiSlotIndex);

		LOG(kNetwork, kDebug, "Server::ClientUnsubscribe Client: {} Slot: {} Coord: ({},{})", iClientId, uiSlotIndex, coord.x, coord.y);
	}

	SendSimplePacket(pClient->pPeer, PacketType::kServerUnsubscribeAck, NetworkManager::kuiChannelReliable, ENET_PACKET_FLAG_RELIABLE, uiSlotIndex);
}

void Server::ClientResyncRequest(int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	LOG(kNetwork, kWarning, "Server::ClientResyncRequest Client: {}", iClientId);

	if (std::ranges::contains(mPendingResyncClientIds, iClientId))
	{
		return;
	}

	// Heap: pending resync client-id vector grows on request
	ScopedSuppressAllocationTracking suppress;
	mPendingResyncClientIds.push_back(iClientId);
}

} // namespace engine

#endif // BT_SERVER
