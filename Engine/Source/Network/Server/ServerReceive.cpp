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

void Server::ClientAckStream(std::span<const uint8_t> packetData, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	NetworkMessages::AckStreamEntry entries[NetworkManager::kiMaxEnetCoordSlots] {};
	NetworkMessages::ClientAckStreamMessage message {
		.pEntries = entries,
		.iEntryCapacity = NetworkManager::kiMaxEnetCoordSlots,
	};
	if (!NetworkMessages::Read(packetData, message)
		|| static_cast<int64_t>(packetData.size()) != NetworkMessages::ClientAckStreamMessage::GetSize(message.uiSlotCount))
	{
		RecordContractViolation(iClientId, "ackstream size", packetData[0], static_cast<int64_t>(packetData.size()));
		return;
	}

	int64_t iFloorAdvanceCount = 0;
	for (uint8_t i = 0; i < message.uiSlotCount; ++i)
	{
		const NetworkMessages::AckStreamEntry& rEntry = message.pEntries[i];
		uint8_t uiSlotIndex = rEntry.uiSlotIndex;
		uint16_t uiSlotEpoch = rEntry.uiEpoch;
		int64_t iSlotAckFloor = rEntry.iAckFloor;
		uint64_t uiSlotBitfieldLow = rEntry.uiReceivedBitfieldLow;
		uint64_t uiSlotBitfieldHigh = rEntry.uiReceivedBitfieldHigh;

		// Invalid-index or inactive slots match neither branch below; skip after the reads so the cursor stays aligned
		if (!(uiSlotIndex < std::ssize(pClient->slots) &&
			(pClient->slots.at(uiSlotIndex).subscription.flags & SubscriptionFlags::kActive)))
		{
			continue;
		}

		if (uiSlotEpoch == pClient->slots.at(uiSlotIndex).ack.uiEpoch &&
			iSlotAckFloor >= pClient->slots.at(uiSlotIndex).ack.iAckFloor)
		{
			// Clamp to server's latest sent tick to prevent future ACK floors
			iSlotAckFloor = std::min(iSlotAckFloor, miLatestBufferedTick);
			AckState& rAckState = pClient->slots.at(uiSlotIndex).ack;
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
		else if (uiSlotEpoch != pClient->slots.at(uiSlotIndex).ack.uiEpoch)
		{
			LOG(kNetwork, kVerbose, "Server::ClientAckStream EpochMismatch Client: {} Slot: {} ClientEpoch: {} ServerEpoch: {}", iClientId, uiSlotIndex, uiSlotEpoch, pClient->slots.at(uiSlotIndex).ack.uiEpoch);
		}
	}

	if (iFloorAdvanceCount > 0)
	{
		if (pClient->bFloorStalled)
		{
			if (pClient->iPeakConsecutiveStallAcks >= kiFloorStallLogThreshold)
			{
				LOG(kNetwork, kVerbose, "Server::ClientAckStream FloorStallResolved Client: {} PeakStalledAcks: {} Slots: {}",
					iClientId, pClient->iPeakConsecutiveStallAcks, message.uiSlotCount);
			}
			pClient->bFloorStalled = false;
			pClient->iPeakConsecutiveStallAcks = 0;
		}
		pClient->iConsecutiveZeroAdvanceAcks = 0;
	}
	else if (message.uiSlotCount > 0)
	{
		++pClient->iConsecutiveZeroAdvanceAcks;
		pClient->iPeakConsecutiveStallAcks = std::max(pClient->iPeakConsecutiveStallAcks, pClient->iConsecutiveZeroAdvanceAcks);
		if (pClient->iConsecutiveZeroAdvanceAcks >= 3)
		{
			pClient->bFloorStalled = true;
		}
	}

	// Pipeline RTT: store client timestamp for echo in SendUpdate (monotonically increasing to guard against out-of-order packets)
	int64_t iClientTimestampNs = message.iTimestampNs;
	if (iClientTimestampNs > pClient->iClientTimestampNs)
	{
		pClient->iClientTimestampNs = iClientTimestampNs;
	}
}

void Server::ClientDesyncReport(std::span<const uint8_t> packetData, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	NetworkMessages::ClientDesyncReportMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now < pClient->desyncReportDeadline)
	{
		return;
	}
	pClient->desyncReportDeadline = now + kDesyncDiagnosticCooldown;

	int64_t iTick = message.iTick;
	GridCoord coord = message.coord;
	uint64_t uiExpectedCrc = message.uiExpectedCrc;
	uint64_t uiActualCrc = message.uiActualCrc;

	char pcExpected[20] {};
	char pcActual[20] {};
	LOG(kNetwork, kError, "Server::ClientDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), uiExpectedCrc), common::ToHex(std::span(pcActual), uiActualCrc));
}

void Server::ClientDebugFrameRequest(std::span<const uint8_t> packetData, ENetPeer* pPeer, int64_t iClientId)
{
	ClientConnection* pClient = FindHandshakenClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	NetworkMessages::ClientDebugFrameRequestMessage message {};
	if (!NetworkMessages::Read(packetData, message))
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
		int64_t iTick = message.iTick;
		GridCoord coord = message.coord;

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

		NetworkMessages::ServerDebugFrameMessage response {
			.iTick = iTick,
			.coord = coord,
			.iUncompressedSize = static_cast<int32_t>(rFrameData.size()),
			.compressedPayload = {.pData = mCompressionBuffer.data(), .iSize = static_cast<int32_t>(iCompressedSize)},
		};
		NetworkMessages::Write(rWorkbuffer, response);

		NetworkManager::SendPacket(pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
	}
}

void Server::ClientHello(std::span<const uint8_t> packetData, ENetPeer* pPeer, int64_t iClientId)
{
	NetworkMessages::ClientHelloMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint32_t uiClientProtocolVersion = message.uiProtocolVersion;
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

	int64_t iClientFrameVersion = message.iFrameVersion;
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

	common::crc_t clientPackIntegrityToken = message.packIntegrityToken;
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

	char pcClientConfig[64] = {};
	size_t iCopyLength = std::min(message.buildConfig.size(), sizeof(pcClientConfig) - 1);
	std::memcpy(pcClientConfig, message.buildConfig.data(), iCopyLength);

	if (std::strcmp(pcClientConfig, kpcBuildConfigName) != 0)
	{
		LOG(kNetwork, kWarning, "Server::ClientHello Client {} build config mismatch: server is {}, client is {}", iClientId, kpcBuildConfigName, pcClientConfig);
	}

	ClientGuid clientGuid = message.bHasGuid ? message.guid : ClientGuid {};

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

void Server::ClientSubscribe(std::span<const uint8_t> packetData, int64_t iClientId)
{
	NetworkMessages::ClientSubscribeMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	GridCoord coord = message.coord;

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

	pClient->slots.at(iSlot).subscription.coord = coord;
	pClient->slots.at(iSlot).subscription.flags.Set(SubscriptionFlags::kActive);
	++pClient->slots.at(iSlot).ack.uiEpoch;

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

void Server::ClientUnsubscribe(std::span<const uint8_t> packetData, int64_t iClientId)
{
	NetworkMessages::ClientUnsubscribeMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint8_t uiSlotIndex = message.uiSlotIndex;
	uint16_t uiEpoch = message.uiEpoch;

	ClientConnection* pClient = FindClient(iClientId);
	if (pClient == nullptr)
	{
		return;
	}

	if (uiSlotIndex < std::ssize(pClient->slots)
		&& (pClient->slots.at(uiSlotIndex).subscription.flags & SubscriptionFlags::kActive)
		&& pClient->slots.at(uiSlotIndex).ack.uiEpoch == uiEpoch)
	{
		GridCoord coord = pClient->slots.at(uiSlotIndex).subscription.coord;
		pClient->FreeSlot(uiSlotIndex);

		LOG(kNetwork, kDebug, "Server::ClientUnsubscribe Client: {} Slot: {} Coord: ({},{})", iClientId, uiSlotIndex, coord.x, coord.y);
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ServerUnsubscribeAckMessage response {.uiSlotIndex = uiSlotIndex};
	NetworkMessages::Write(rWorkbuffer, response);
	NetworkManager::SendPacket(pClient->pPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Server::ClientResyncRequest(std::span<const uint8_t> packetData, int64_t iClientId)
{
	NetworkMessages::ClientResyncRequestMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

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
