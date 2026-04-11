#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

#include "Frame/FrameStaticData.h"
#include "Game.h"
#include "Network/NetworkCursor.h"

namespace engine
{

static std::unique_ptr<game::Frame> DecompressAndReadFrame(const uint8_t*& pCursor, size_t iRemaining)
{
	if (iRemaining < 8) // 4B uncompressed + 4B compressed
	{
		return nullptr;
	}

	int32_t iUncompressedSize = ReadInt32(pCursor);
	int32_t iCompressedSize = ReadInt32(pCursor);

	if (iUncompressedSize <= 0 || iCompressedSize <= 0
		|| static_cast<size_t>(iCompressedSize) > iRemaining - 8)
	{
		return nullptr;
	}

	std::string decompressed(iUncompressedSize, '\0');
	int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(pCursor), decompressed.data(), iCompressedSize, iUncompressedSize);
	pCursor += iCompressedSize;

	if (iDecompressResult != iUncompressedSize)
	{
		return nullptr;
	}

	std::istringstream frameStream(std::move(decompressed), std::ios::binary);
	std::unique_ptr<game::Frame> pFrame = std::make_unique<game::Frame>();
	pFrame->ServerRead(frameStream);
	return pFrame;
}

bool Client::RemoveCancelledSubscription(GridCoord coord)
{
	auto it = std::ranges::find(mCancelledSubscriptions, coord);
	if (it != mCancelledSubscriptions.end())
	{
		mCancelledSubscriptions.erase(it);
		return true;
	}
	return false;
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

void Client::ServerCoordFullState(const uint8_t* pData, size_t iSize)
{
	// 1B type + 1B slot + 2B epoch + 8B tick + 4B gridX + 4B gridY = 20 fixed bytes
	if (iSize < 20)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	LOG(kNetwork, kVerbose, "Client::ServerCoordFullState Frame: {} Slot: {} Coord: ({},{})", iTick, uiSlotIndex, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor, iSize - 20);
	if (pFrame == nullptr)
	{
		LOG(kNetwork, kWarning, "Client::ServerCoordFullState LZ4 decompression failed Coord: ({},{}) Frame: {}", coord.x, coord.y, iTick);
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
			RemoveCancelledSubscription(coord);
			SendUnsubscribeOnly(uiSlotIndex);
			LOG(kNetwork, kVerbose, "Client::ServerCoordFullState coord mismatch, sent unsubscribe for ghost Slot: {} Coord: ({},{}) SlotCoord: ({},{})", uiSlotIndex, coord.x, coord.y, rSlot.coord.x, rSlot.coord.y);
			return;
		}
		// Validate epoch for kWaitingFullState (epoch is set by SubscribeAccept)
		// Guards against stale full-state from a previous subscription to the same coord/slot
		if (rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch != rSlot.ackState.uiEpoch)
		{
			return;
		}
	}
	else
	{
		return;
	}

	// If this coord was cancelled while kSubscribing, reject the full state
	if (RemoveCancelledSubscription(coord))
	{
		SendUnsubscribeOnly(uiSlotIndex);
		LOG(kNetwork, kVerbose, "Client::ServerCoordFullState cancelled, sent unsubscribe for ghost Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
		rSlot = {};
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
	rSlot.ackState.uiReceivedBitfieldLow = 0;
	rSlot.ackState.uiReceivedBitfieldHigh = 0;
	rSlot.ackState.uiEpoch = uiEpoch;
	rSlot.eState = CoordSubscriptionState::kActive;
}

void Client::ServerCoordStaticData(const uint8_t* pData, size_t iSize)
{
	// 1B type + 1B slot + 2B epoch + 4B coord.x + 4B coord.y + 4B size = 16 fixed bytes
	if (iSize < 16)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	int32_t iSize32 = ReadInt32(pCursor);

	if (iSize32 <= 0 || static_cast<size_t>(iSize32) > iSize - 16)
	{
		return;
	}

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	const ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	if (rSlot.eState != CoordSubscriptionState::kWaitingFullState
		&& rSlot.eState != CoordSubscriptionState::kSubscribing
		&& rSlot.eState != CoordSubscriptionState::kUnsubscribed)
	{
		return;
	}

	if ((rSlot.eState == CoordSubscriptionState::kWaitingFullState || rSlot.eState == CoordSubscriptionState::kSubscribing)
		&& uiEpoch != rSlot.ackState.uiEpoch)
	{
		return;
	}

	LOG(kNetwork, kVerbose, "Client::ServerCoordStaticData Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::string staticBytes(reinterpret_cast<const char*>(pCursor), iSize32);
	std::istringstream staticStream(std::move(staticBytes), std::ios::binary);

	ReceivedStaticData received {};
	received.iSlot = uiSlotIndex;
	received.coord = coord;
	received.staticData.Read(staticStream);

	// Heap: received static data vector grows on new subscription
	mReceivedStaticData.push_back(std::move(received));
}

void Client::ServerCoordUpdateOrResend(const uint8_t* pData, size_t iSize, bool bProcessRtt)
{
	// 1B type + 1B slot + 2B epoch + 8B tick + 8B echoTs + 8B sharedCrc + 8B inputCrc + 4B compressedSize = 40 fixed bytes
	if (iSize < 40)
	{
		return;
	}

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

			std::chrono::high_resolution_clock::time_point now = std::chrono::high_resolution_clock::now();
			if (mbHasLastUpdateArrival)
			{
				int64_t iIntervalUs = std::chrono::duration_cast<std::chrono::microseconds>(now - mLastUpdateArrival).count();
				int64_t iExpectedUs = 1'000'000 / kiTickRate;
				int64_t iDeviation = std::abs(iIntervalUs - iExpectedUs);
				mSmoothedJitterUs = iDeviation;
				mSmoothedJitterUs.Update();
			}
			mLastUpdateArrival = now;
			mbHasLastUpdateArrival = true;
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

	common::crc_t sharedCrc = static_cast<common::crc_t>(ReadUint64(pCursor));
	common::crc_t inputCrc = static_cast<common::crc_t>(ReadUint64(pCursor));
	int32_t iCompressedSize = ReadInt32(pCursor);

	if (iCompressedSize < 0 || static_cast<size_t>(iCompressedSize) > iSize - 40)
	{
		return;
	}

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	ReceivedCoordUpdate update {};
	update.iTick = iTick;
	update.sharedCrc = sharedCrc;
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

void Client::ServerDebugFrame(const uint8_t* pData, size_t iSize)
{
	// 1B type + 8B tick + 4B gridX + 4B gridY = 17 fixed bytes
	if (iSize < 17)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iTick = ReadInt64(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);
	LOG(kNetwork, kDebug, "Client::ServerDebugFrame Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(pCursor, iSize - 17);
	if (pFrame == nullptr)
	{
		LOG(kNetwork, kWarning, "Client::ServerDebugFrame LZ4 decompression failed Frame: {}", iTick);
		return;
	}

	mpReceivedDebugFrame = std::make_unique<ReceivedDebugFrame>();
	mpReceivedDebugFrame->iTick = iTick;
	mpReceivedDebugFrame->coord = coord;
	mpReceivedDebugFrame->pFrame = std::move(pFrame);
}

void Client::ServerConnectionResponse(const uint8_t* pData, size_t iSize)
{
	// 1B type + 1B accepted = 2 minimum bytes
	if (iSize < 2)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1;
	bool bAccepted = (ReadUint8(pCursor) != 0);

	if (bAccepted)
	{
		mbConnectionAccepted = true;

		// Read assigned GUID (1B type + 1B accepted + 8B high + 8B low = 18 bytes)
		if (iSize >= 18)
		{
			mClientGuid.uiHigh = ReadUint64(pCursor);
			mClientGuid.uiLow = ReadUint64(pCursor);
			LOG(kNetwork, kInfo, "Client GUID assigned: {} {}", mClientGuid.uiHigh, mClientGuid.uiLow);

			// Persist to disk
			ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
			std::fstream guidStream = gpFileManager->OpenFile({FileFlags::kAppDataDirectory, FileFlags::kWrite}, std::filesystem::path("ClientGuid.bin"));
			int64_t iGuidVersion = 1;
			common::Write(guidStream, iGuidVersion);
			int64_t iGuidSize = 0;
			common::Write(guidStream, iGuidSize);
			common::Write(guidStream, mClientGuid.uiHigh);
			common::Write(guidStream, mClientGuid.uiLow);
		}

		// Seed smoothed pipeline RTT from ENet's handshake measurement so early frames have a reasonable value
		if (mpServerPeer != nullptr)
		{
			int64_t iRttUs = static_cast<int64_t>(mpServerPeer->roundTripTime) * 1000;
			if (iRttUs > 0)
			{
				mSmoothedPipelineRttUs.Seed(iRttUs);
			}
			LOG(kNetwork, kDebug, "Client::ServerConnectionResponse Handshake RTT: {} us", iRttUs);
		}
	}
	else
	{
		size_t iMessageLength = iSize - 2;
		size_t iCopyLength = std::min(iMessageLength, sizeof(mpcRejectionReason) - 1);
		std::memcpy(mpcRejectionReason, pCursor, iCopyLength);
		mpcRejectionReason[iCopyLength] = '\0';
		LOG(kNetwork, kWarning, "Client::ServerConnectionResponse Rejected: {}", mpcRejectionReason);
	}
}

void Client::ServerSubscribeAccept(const uint8_t* pData, size_t iSize)
{
	// 1B type + 1B slot + 2B epoch + 4B gridX + 4B gridY = 12 fixed bytes
	if (iSize < 12)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	uint8_t uiSlotIndex = ReadUint8(pCursor);
	uint16_t uiEpoch = ReadUint16(pCursor);
	GridCoord coord = ReadGridCoord(pCursor);

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		// Server rejected subscription (no free slot) — clear the kSubscribing placeholder
		ClearSubscribingPlaceholder(coord);
		LOG(kNetwork, kWarning, "Client::ServerSubscribeAccept Rejected Coord: ({},{})", coord.x, coord.y);
		return;
	}

	// Validate target slot FIRST (before clearing placeholder)
	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	bool bTargetIsPlaceholder = (rSlot.eState == CoordSubscriptionState::kSubscribing && rSlot.coord == coord);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribed && !bTargetIsPlaceholder)
	{
		// If the slot is already active for the same coord, the accept is from a re-subscription
		// whose stale predecessor data already activated the slot. Update the epoch instead of ghost-killing it.
		if (rSlot.eState == CoordSubscriptionState::kActive && rSlot.coord == coord)
		{
			rSlot.ackState.uiEpoch = uiEpoch;
			ClearSubscribingPlaceholder(coord);
			if (RemoveCancelledSubscription(coord))
			{
				LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Healed then cancelled Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
				SendUnsubscribe(uiSlotIndex);
				return;
			}
			LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Healed active slot Slot: {} Coord: ({},{}) Epoch: {}", uiSlotIndex, coord.x, coord.y, uiEpoch);
			return;
		}

		LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Ignoring Slot: {} Coord: ({},{}) SlotCoord: ({},{}) State: {}", uiSlotIndex, coord.x, coord.y, rSlot.coord.x, rSlot.coord.y, static_cast<int>(rSlot.eState));
		SendUnsubscribeOnly(uiSlotIndex);
		RemoveCancelledSubscription(coord);
		LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept sent unsubscribe for ghost Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
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
	rSlot.ackState.uiReceivedBitfieldLow = 0;
	rSlot.ackState.uiReceivedBitfieldHigh = 0;
	rSlot.ackState.uiEpoch = uiEpoch;

	// If this coord was cancelled while kSubscribing, immediately unsubscribe
	if (RemoveCancelledSubscription(coord))
	{
		LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Cancelled Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
		SendUnsubscribe(uiSlotIndex);
		return;
	}

	LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
}

void Client::ServerUnsubscribeAck(const uint8_t* pData, size_t iSize)
{
	// 1B type + 1B slot = 2 fixed bytes
	if (iSize < 2)
	{
		return;
	}

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

	LOG(kNetwork, kVerbose, "Client::ServerUnsubscribeAck Slot: {} Coord: ({},{})", uiSlotIndex, rSlot.coord.x, rSlot.coord.y);

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

void Client::ServerTimespeedUpdate(const uint8_t* pData, size_t iSize)
{
	// [1B type][8B multiply][8B divide]
	if (iSize < 17)
	{
		return;
	}

	const uint8_t* pCursor = pData + 1; // Skip packet type

	int64_t iMultiply = ReadInt64(pCursor);
	int64_t iDivide = ReadInt64(pCursor);

	LOG(kNetwork, kDebug, "Client::ServerTimespeedUpdate Multiply: {} Divide: {}", iMultiply, iDivide);
	game::gpGame->mTimeStep.SetTimeScale(iMultiply, iDivide);
}

} // namespace engine

#endif // BT_CLIENT
