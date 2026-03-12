#include "Pch.h"

#include "Network/ClientNetwork/ClientNetwork.h"

#include "Memory/MemoryManager.h"
#include "Network/NetworkCursor.h"

namespace engine
{

static std::unique_ptr<game::Frame> DecompressAndReadFrame(const uint8_t*& pCursor)
{
	int32_t iUncompressedSize = ReadInt32(pCursor);
	int32_t iCompressedSize = ReadInt32(pCursor);

	std::string decompressed(iUncompressedSize, '\0');
	int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pCursor), decompressed.data(), iCompressedSize, iUncompressedSize);
	pCursor += iCompressedSize;

	if (iDecompressResult < 0)
	{
		return nullptr;
	}

	std::istringstream frameStream(std::move(decompressed), std::ios::binary);
	auto pFrame = std::make_unique<game::Frame>();
	pFrame->ServerRead(frameStream);
	return pFrame;
}

void ClientNetwork::ClearSubscribingPlaceholder(GridCoord coord)
{
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots[i].eState == CoordSubscriptionState::kSubscribing && mCoordSlots[i].coord == coord)
		{
			mCoordSlots[i] = {};
			break;
		}
	}
}

void ClientNetwork::HandleServerAssignPlayer(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iPlayerIdValue = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	// Heap: received assignments vector grows on new assignment packets
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	mReceivedAssignments.push_back({game::player_t(uuid_t(iPlayerIdValue)), coord});

	common::Log("NetworkClient: Assigned player ID {} at grid ({},{})", iPlayerIdValue, coord.x, coord.y);
}

void ClientNetwork::HandleServerPlayerState(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	PlayerStateType eStateType = static_cast<PlayerStateType>(ReadUint8(pCursor));
	int64_t iPlayerIdValue = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	// Heap: received player states vector grows on state packets
	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
	mReceivedPlayerStates.push_back({eStateType, game::player_t(uuid_t(iPlayerIdValue)), coord});

	common::Log("NetworkClient: Player {} state {} at grid ({},{})", iPlayerIdValue, static_cast<int>(eStateType), coord.x, coord.y);
}

void ClientNetwork::HandleServerCoordFullState(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	common::Log("NetworkClient: Received coord full state frame {} slot {} coord ({},{})", iTick, uiSlotIndex, coord.x, coord.y);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		common::Log("NetworkClient: LZ4 decompression failed for full state coord ({},{}) frame {}", coord.x, coord.y, iTick);
		return;
	}

	// Validate slot before pushing full state
	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		common::Log("NetworkClient: FullState rejected - slot {} out of range", uiSlotIndex); // DT: TEMP
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];

	// Full state can arrive before subscribe accept (different ENet channels)
	// If the slot is kUnsubscribed, the full state arrived first — clear the kSubscribing placeholder
	if (rSlot.eState == CoordSubscriptionState::kUnsubscribed)
	{
		ClearSubscribingPlaceholder(coord);
		rSlot.coord = coord;
	}
	else if (rSlot.eState == CoordSubscriptionState::kWaitingFullState || rSlot.eState == CoordSubscriptionState::kSubscribing)
	{
		// Validate coord matches to prevent stale full state from a previous subscription
		if (rSlot.coord != coord)
		{
			common::Log("NetworkClient: FullState rejected - coord mismatch slot {} expected ({},{}) got ({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y, coord.x, coord.y); // DT: TEMP
			return;
		}
	}
	else
	{
		common::Log("NetworkClient: FullState rejected - slot {} in state {}", uiSlotIndex, static_cast<int>(rSlot.eState)); // DT: TEMP
		return;
	}

	ReceivedCoordFullState fullState {};
	fullState.iTick = iTick;
	fullState.coord = coord;
	fullState.iSlot = uiSlotIndex;
	fullState.pFrame = std::move(pFrame);

	// Heap: received full states vector grows on new cell data
	mReceivedFullStates.push_back(std::move(fullState));

	rSlot.iAckFloor = iTick;
	rSlot.uiReceivedBitfield = 0;
	rSlot.uiEpoch = uiEpoch;
	rSlot.eState = CoordSubscriptionState::kActive;
	FILE_LOG(0, "[NetworkClient] Slot {} now Active: coord=({},{}) ackFloor={}", uiSlotIndex, coord.x, coord.y, iTick);
}

void ClientNetwork::HandleServerCoordUpdateOrResend(const uint8_t* pData, bool bProcessRtt)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iTick = ReadInt64(pCursor);

	// Pipeline RTT: read echoed client timestamp (monotonic guard prevents duplicate processing during multi-frame ticks)
	int64_t iEchoedTimestampNs = ReadInt64(pCursor);
	if (bProcessRtt && iEchoedTimestampNs > 0 && iEchoedTimestampNs > miLastEchoedTimestampNs)
	{
		miLastEchoedTimestampNs = iEchoedTimestampNs;
		int64_t iNowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		int64_t iRttUs = (iNowNs - iEchoedTimestampNs) / 1000;
		mSmoothedPipelineRttUs = iRttUs;
		mSmoothedPipelineRttUs.Update();
	}

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}
	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	bool bWaitingFullState = rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch == rSlot.uiEpoch;
	if (!bWaitingFullState && (rSlot.eState != CoordSubscriptionState::kActive || uiEpoch != rSlot.uiEpoch))
	{
		return;
	}

	common::crc_t serverCrc = static_cast<common::crc_t>(ReadUint64(pCursor));
	common::crc_t inputCrc = static_cast<common::crc_t>(ReadUint64(pCursor));
	int32_t iCompressedSize = ReadInt32(pCursor);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedCoordUpdate update {};
	update.iTick = iTick;
	update.serverCrc = serverCrc;
	update.inputCrc = inputCrc;

	if (iCompressedSize > 0)
	{
		// Heap: status changes vector
		update.statusChanges.resize(kiMaxStatusChangesPerCell);
		int64_t iCount = DecompressStatusChangeBatch(pCursor, iCompressedSize, update.statusChanges.data(), kiMaxStatusChangesPerCell);
		update.statusChanges.resize(iCount);
		pCursor += iCompressedSize;
	}

	// Heap: received updates vector grows each tick
	mReceivedCoordUpdates[uiSlotIndex].push_back(std::move(update));
	if (!bWaitingFullState)
	{
		TrackReceivedTick(uiSlotIndex, iTick);
	}

	common::Log("NetworkClient: Received coord ({},{}) slot {} frame {} (resend={})", rSlot.coord.x, rSlot.coord.y, uiSlotIndex, iTick, !bProcessRtt); // DT: TEMP
}

void ClientNetwork::HandleServerDebugFrame(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	common::Log("NetworkClient: Received debug frame {} grid ({},{})", iTick, coord.x, coord.y);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		common::Log("NetworkClient: LZ4 decompression failed for debug frame {}", iTick);
		return;
	}

	mpReceivedDebugFrame = std::make_unique<ReceivedDebugFrame>();
	mpReceivedDebugFrame->iTick = iTick;
	mpReceivedDebugFrame->coord = coord;
	mpReceivedDebugFrame->pFrame = std::move(pFrame);
}

void ClientNetwork::HandleServerConnectionResponse(const uint8_t* pData, size_t iSize)
{
	const uint8_t* pCursor = pData + 1;
	bool bAccepted = (ReadUint8(pCursor) != 0);

	if (bAccepted)
	{
		mbConnectionAccepted = true;
		common::Log("NetworkClient: Connection accepted");
	}
	else
	{
		size_t iMessageLength = iSize - 2;
		size_t iCopyLength = std::min(iMessageLength, sizeof(mpcRejectionReason) - 1);
		std::memcpy(mpcRejectionReason, pCursor, iCopyLength);
		mpcRejectionReason[iCopyLength] = '\0';
		common::Log("NetworkClient: Connection rejected: {}", mpcRejectionReason);
	}
}

void ClientNetwork::HandleServerSubscribeAccept(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		// Server rejected subscription (no free slot) — clear the kSubscribing placeholder
		ClearSubscribingPlaceholder(coord);
		common::Log("NetworkClient: Subscribe rejected for coord ({},{})", coord.x, coord.y);
		return;
	}

	// Validate target slot FIRST (before clearing placeholder)
	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	bool bTargetIsPlaceholder = (rSlot.eState == CoordSubscriptionState::kSubscribing && rSlot.coord == coord);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribed && !bTargetIsPlaceholder)
	{
		common::Log("NetworkClient: Subscribe accept for slot {} but slot is in state {}, ignoring", uiSlotIndex, static_cast<int>(rSlot.eState));
		return;
	}

	// Clear the client-side kSubscribing placeholder (may be at a different slot index than the server assigned)
	if (!bTargetIsPlaceholder)
	{
		ClearSubscribingPlaceholder(coord);
	}

	rSlot.coord = coord;
	rSlot.eState = CoordSubscriptionState::kWaitingFullState;
	rSlot.iAckFloor = -1;
	rSlot.uiReceivedBitfield = 0;
	rSlot.uiEpoch = uiEpoch;

	FILE_LOG(0, "[NetworkClient] SubscribeAccept: slot={} coord=({},{})", uiSlotIndex, coord.x, coord.y);
	common::Log("NetworkClient: Subscribe accepted slot {} coord ({},{})", uiSlotIndex, coord.x, coord.y);
}

void ClientNetwork::HandleServerUnsubscribeAck(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots[uiSlotIndex];
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribing)
	{
		return;
	}

	FILE_LOG(0, "[NetworkClient] UnsubscribeAck: slot={} coord=({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);
	common::Log("NetworkClient: Unsubscribe ack slot {} coord ({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);

	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		uint8_t uiSlot = uiSlotIndex;
		std::erase_if(mDelayedPackets, [uiSlot](const DelayedPacket& rPacket)
		{
			return NetworkManager::IsCoordChannel(rPacket.uiChannelId) && NetworkManager::ChannelToSlot(rPacket.uiChannelId) == uiSlot;
		});
	}

	rSlot = {};
}

} // namespace engine
