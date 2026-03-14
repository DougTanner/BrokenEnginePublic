#include "Pch.h"

#include "Network/Client/Client.h"

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

void Client::ClearSubscribingPlaceholder(GridCoord coord)
{
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kSubscribing && mCoordSlots.at(i).coord == coord)
		{
			mCoordSlots.at(i) = {};
			break;
		}
	}
}

void Client::ServerCoordFullState(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	Log(kLogNetwork, "Client::ServerCoordFullState Frame: {} Slot: {} Coord: ({},{})", iTick, uiSlotIndex, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		Log(kLogNetwork, "Client::ServerCoordFullState LZ4 decompression failed Coord: ({},{}) Frame: {}", coord.x, coord.y, iTick);
		return;
	}

	// Validate slot before pushing full state
	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);

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
			return;
		}
	}
	else
	{
		return;
	}

	ReceivedCoordFullState fullState {};
	fullState.iTick = iTick;
	fullState.coord = coord;
	fullState.iSlot = uiSlotIndex;
	fullState.pFrame = std::move(pFrame);

	// Heap: received full states vector grows on new cell data
	mReceivedFullStates.push_back(std::move(fullState));

	rSlot.ackState.iAckFloor = iTick;
	rSlot.ackState.uiReceivedBitfield = 0;
	rSlot.ackState.uiEpoch = uiEpoch;
	rSlot.eState = CoordSubscriptionState::kActive;
}

void Client::ServerCoordUpdateOrResend(const uint8_t* pData, bool bProcessRtt)
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
		if (iRttUs >= 0)
		{
			mSmoothedPipelineRttUs = iRttUs;
			mSmoothedPipelineRttUs.Update();
		}
	}

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}
	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	bool bWaitingFullState = rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch == rSlot.ackState.uiEpoch;
	if (!bWaitingFullState && (rSlot.eState != CoordSubscriptionState::kActive || uiEpoch != rSlot.ackState.uiEpoch))
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
	mReceivedCoordUpdates.at(uiSlotIndex).push_back(std::move(update));
	if (!bWaitingFullState)
	{
		TrackReceivedTick(uiSlotIndex, iTick);
	}

}

void Client::ServerDebugFrame(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	Log(kLogNetwork, "Client::ServerDebugFrame Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor);
	if (pFrame == nullptr)
	{
		Log(kLogNetwork, "Client::ServerDebugFrame LZ4 decompression failed Frame: {}", iTick);
		return;
	}

	mpReceivedDebugFrame = std::make_unique<ReceivedDebugFrame>();
	mpReceivedDebugFrame->iTick = iTick;
	mpReceivedDebugFrame->coord = coord;
	mpReceivedDebugFrame->pFrame = std::move(pFrame);
}

void Client::ServerConnectionResponse(const uint8_t* pData, size_t iSize)
{
	const uint8_t* pCursor = pData + 1;
	bool bAccepted = (ReadUint8(pCursor) != 0);

	if (bAccepted)
	{
		mbConnectionAccepted = true;
	}
	else
	{
		size_t iMessageLength = iSize - 2;
		size_t iCopyLength = std::min(iMessageLength, sizeof(mpcRejectionReason) - 1);
		std::memcpy(mpcRejectionReason, pCursor, iCopyLength);
		mpcRejectionReason[iCopyLength] = '\0';
		Log(kLogNetwork, "Client::ServerConnectionResponse Rejected: {}", mpcRejectionReason);
	}
}

void Client::ServerSubscribeAccept(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		// Server rejected subscription (no free slot) — clear the kSubscribing placeholder
		ClearSubscribingPlaceholder(coord);
		Log(kLogNetwork, "Client::ServerSubscribeAccept Rejected Coord: ({},{})", coord.x, coord.y);
		return;
	}

	// Validate target slot FIRST (before clearing placeholder)
	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	bool bTargetIsPlaceholder = (rSlot.eState == CoordSubscriptionState::kSubscribing && rSlot.coord == coord);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribed && !bTargetIsPlaceholder)
	{
		Log(kLogNetwork, "Client::ServerSubscribeAccept Ignoring Slot: {} State: {}", uiSlotIndex, static_cast<int>(rSlot.eState));
		return;
	}

	// Clear the client-side kSubscribing placeholder (may be at a different slot index than the server assigned)
	if (!bTargetIsPlaceholder)
	{
		ClearSubscribingPlaceholder(coord);
	}

	rSlot.coord = coord;
	rSlot.eState = CoordSubscriptionState::kWaitingFullState;
	rSlot.ackState.iAckFloor = -1;
	rSlot.ackState.uiReceivedBitfield = 0;
	rSlot.ackState.uiEpoch = uiEpoch;

	Log(kLogNetwork, "Client::ServerSubscribeAccept Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
}

void Client::ServerUnsubscribeAck(const uint8_t* pData)
{
	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribing)
	{
		return;
	}

	Log(kLogNetwork, "Client::ServerUnsubscribeAck Slot: {} Coord: ({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);

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
