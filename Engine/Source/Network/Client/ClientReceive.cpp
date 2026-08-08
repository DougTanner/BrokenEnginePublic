#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

#include "Game.h"
#include "Frame/FrameStaticData.h"
#include "Network/NetworkCursor.h"

namespace engine
{

static std::unique_ptr<game::Frame> DecompressAndReadFrame(int32_t iUncompressedSize, const NetworkMessages::PacketPayload& rCompressedPayload)
{
	if (iUncompressedSize <= 0 || rCompressedPayload.iSize <= 0
		|| static_cast<int64_t>(iUncompressedSize) > kiMaxUncompressedFrameBytes
		|| rCompressedPayload.pData == nullptr)
	{
		return nullptr;
	}

	std::string decompressed(iUncompressedSize, '\0');
	int iDecompressResult = LZ4_decompress_safe(reinterpret_cast<const char*>(rCompressedPayload.pData), decompressed.data(), rCompressedPayload.iSize, iUncompressedSize);

	if (iDecompressResult != iUncompressedSize)
	{
		return nullptr;
	}

	std::istringstream frameStream(std::move(decompressed), std::ios::binary);
	std::unique_ptr<game::Frame> pFrame = std::make_unique<game::Frame>();
	game::NetworkSessionContract::ReadFrame(frameStream, *pFrame);
	if (!frameStream)
	{
		return nullptr;
	}
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

int64_t Client::FindSubscribingPlaceholder(GridCoord coord) const
{
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kSubscribing && mCoordSlots.at(i).coord == coord)
		{
			return i;
		}
	}
	return -1;
}

void Client::ClearSubscribingPlaceholder(GridCoord coord)
{
	int64_t iSlot = FindSubscribingPlaceholder(coord);
	if (iSlot >= 0)
	{
		mCoordSlots.at(iSlot) = {};
	}
}

Client::FullStateFlags_t Client::ClassifyFullState(uint8_t uiSlotIndex, uint16_t uiEpoch, GridCoord coord)
{
	const ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);

	// Full state arrived before SubscribeAccept (different ENet channels)
	if (rSlot.eState == CoordSubscriptionState::kUnsubscribed)
	{
		if (FindSubscribingPlaceholder(coord) >= 0)
		{
			return { FullStateFlags::kClearPlaceholder, FullStateFlags::kCommit };
		}
		return FullStateFlags::kRejectAsGhost;
	}

	if (rSlot.eState == CoordSubscriptionState::kWaitingFullState
		|| rSlot.eState == CoordSubscriptionState::kSubscribing)
	{
		// Stale full-state from a previous subscription to a different coord
		if (rSlot.coord != coord)
		{
			return FullStateFlags::kRejectAsGhost;
		}
		// Epoch guard: SubscribeAccept set the epoch; stale full-state on the same coord/slot is dropped
		if (rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch != rSlot.ackState.uiEpoch)
		{
			return {};
		}
		return FullStateFlags::kCommit;
	}

	// kActive: resync full-state re-commit when coord+epoch match (desync recovery).
	// A genuine resend re-activates the slot at the resend tick via ServerCoordFullState's
	// commit block; a stale/ghost full state (wrong coord or superseded epoch) still falls
	// through to the reject below.
	if (rSlot.eState == CoordSubscriptionState::kActive
		&& rSlot.coord == coord
		&& uiEpoch == rSlot.ackState.uiEpoch)
	{
		return FullStateFlags::kCommit;
	}

	// kActive (mismatched) or kUnsubscribing — silent reject
	return {};
}

Client::CoordUpdateFlags_t Client::ClassifyCoordUpdate(uint8_t uiSlotIndex, uint16_t uiEpoch)
{
	const ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);

	if (rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch == rSlot.ackState.uiEpoch)
	{
		// Pre-full-state buffering: accept but do not advance the per-slot tick counter
		return CoordUpdateFlags::kCommit;
	}
	if (rSlot.eState == CoordSubscriptionState::kActive && uiEpoch == rSlot.ackState.uiEpoch)
	{
		return { CoordUpdateFlags::kCommit, CoordUpdateFlags::kTrackTick };
	}
	return {};
}

Client::SubscribeAcceptFlags_t Client::ClassifySubscribeAccept(uint8_t uiSlotIndex, GridCoord coord)
{
	const ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	bool bTargetIsPlaceholder = rSlot.eState == CoordSubscriptionState::kSubscribing && rSlot.coord == coord;

	// Re-subscription whose stale predecessor data already activated the slot — heal in place
	if (rSlot.eState == CoordSubscriptionState::kActive && rSlot.coord == coord)
	{
		return { SubscribeAcceptFlags::kHealEpoch, SubscribeAcceptFlags::kClearPlaceholder };
	}

	// State mismatch (and not active-coord-match) — ghost reject
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribed && !bTargetIsPlaceholder)
	{
		return SubscribeAcceptFlags::kRejectGhost;
	}

	// Commit-init: target slot is either kUnsubscribed or already the kSubscribing placeholder
	if (bTargetIsPlaceholder)
	{
		return SubscribeAcceptFlags::kCommitInit;
	}
	return { SubscribeAcceptFlags::kClearPlaceholder, SubscribeAcceptFlags::kCommitInit };
}

void Client::ServerCoordFullState(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerCoordFullStateMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint8_t uiSlotIndex = message.uiSlotIndex;
	uint16_t uiEpoch = message.uiEpoch;
	int64_t iTick = message.iTick;
	GridCoord coord = message.coord;

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	LOG(kNetwork, kVerbose, "Client::ServerCoordFullState Frame: {} Slot: {} Coord: ({},{})", iTick, uiSlotIndex, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	ScopedSuppressAllocationTracking suppress;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(message.iUncompressedSize, message.compressedPayload);
	if (pFrame == nullptr)
	{
		LOG(kNetwork, kWarning, "Client::ServerCoordFullState LZ4 decompression or frame read failed Coord: ({},{}) Frame: {}", coord.x, coord.y, iTick);
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	FullStateFlags_t actions = ClassifyFullState(uiSlotIndex, uiEpoch, coord);

	if (actions & FullStateFlags::kRejectAsGhost)
	{
		RemoveCancelledSubscription(coord);
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		NetworkMessages::ClientUnsubscribeMessage unsubscribe {.uiSlotIndex = uiSlotIndex, .uiEpoch = uiEpoch};
		NetworkMessages::Write(rWorkbuffer, unsubscribe);
		NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
		LOG(kNetwork, kVerbose, "Client::ServerCoordFullState coord mismatch, sent unsubscribe for ghost Slot: {} Coord: ({},{}) SlotCoord: ({},{})", uiSlotIndex, coord.x, coord.y, rSlot.coord.x, rSlot.coord.y);
		return;
	}

	if (!(actions & FullStateFlags::kCommit))
	{
		return;
	}

	if (actions & FullStateFlags::kClearPlaceholder)
	{
		ClearSubscribingPlaceholder(coord);
		rSlot.coord = coord;
	}

	// Late cancellation: the kSubscribing slot was dropped between subscribe and full-state arrival
	if (RemoveCancelledSubscription(coord))
	{
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		NetworkMessages::ClientUnsubscribeMessage unsubscribe {.uiSlotIndex = uiSlotIndex, .uiEpoch = uiEpoch};
		NetworkMessages::Write(rWorkbuffer, unsubscribe);
		NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
		LOG(kNetwork, kVerbose, "Client::ServerCoordFullState cancelled, sent unsubscribe for ghost Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
		rSlot = {};
		return;
	}

	ReceivedCoordFullState fullState {};
	fullState.iTick = iTick;
	fullState.coord = coord;
	fullState.pFrame = std::move(pFrame);

	// Heap: received full states vector grows on new cell data
	mReceivedFullStates.push_back(std::move(fullState));

	rSlot.ackState.iAckFloor = iTick;
	rSlot.ackState.uiReceivedBitfieldLow = 0;
	rSlot.ackState.uiReceivedBitfieldHigh = 0;
	rSlot.ackState.uiEpoch = uiEpoch;
	rSlot.eState = CoordSubscriptionState::kActive;
	rSlot.transitionStartTime = {};
}

void Client::ServerCoordStaticData(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerCoordStaticDataMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint8_t uiSlotIndex = message.uiSlotIndex;
	uint16_t uiEpoch = message.uiEpoch;
	GridCoord coord = message.coord;
	if (message.staticData.iSize <= 0)
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

	// Stale static data from a previous subscription to a different coord (recycled slot) — silently drop (the full-state path owns ghost unsubscribe)
	if ((rSlot.eState == CoordSubscriptionState::kWaitingFullState || rSlot.eState == CoordSubscriptionState::kSubscribing)
		&& rSlot.coord != coord)
	{
		return;
	}
	// Epoch guard: SubscribeAccept set the epoch; a kSubscribing placeholder has none yet, so this applies only to kWaitingFullState
	if (rSlot.eState == CoordSubscriptionState::kWaitingFullState && uiEpoch != rSlot.ackState.uiEpoch)
	{
		return;
	}

	ScopedSuppressAllocationTracking suppress;

	std::string staticBytes(reinterpret_cast<const char*>(message.staticData.pData), message.staticData.iSize);
	std::istringstream staticStream(std::move(staticBytes), std::ios::binary);

	ReceivedStaticData received {};
	received.coord = coord;
	received.staticData.Read(staticStream, /*bIncludeNavData=*/true);
	if (!staticStream)
	{
		LOG(kNetwork, kWarning, "Client::ServerCoordStaticData static data read failed Coord: ({},{}) Slot: {}", coord.x, coord.y, uiSlotIndex);
		return;
	}

	// Heap: received static data vector grows on new subscription
	mReceivedStaticData.push_back(std::move(received));
}

void Client::ServerCoordUpdateOrResend(std::span<const uint8_t> packetData, bool bProcessRtt)
{
	auto receive = [this, bProcessRtt](const NetworkMessages::CoordUpdateFields& rMessage)
	{
		// Pipeline RTT: read echoed client timestamp (monotonic guard prevents duplicate processing during multi-frame ticks)
		if (bProcessRtt && rMessage.iEchoedTimestampNs > 0 && rMessage.iEchoedTimestampNs > miLastEchoedTimestampNs)
		{
			miLastEchoedTimestampNs = rMessage.iEchoedTimestampNs;
			int64_t iNowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			int64_t iRttUs = (iNowNs - rMessage.iEchoedTimestampNs) / 1000;
			if (iRttUs >= 0)
			{
				mSmoothedPipelineRttUs = iRttUs;
				mSmoothedPipelineRttUs.Update();

				std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
				if (mStateFlags & ClientStateFlags::kHasLastUpdateArrival)
				{
					int64_t iIntervalUs = std::chrono::duration_cast<std::chrono::microseconds>(now - mLastUpdateArrival).count();
					// Server broadcast cadence is wall-scaled by the debug timescale; expect the scaled wall interval, not the fixed sim tick period
					int64_t iExpectedMicroseconds = mTimeState.iExpectedUpdateIntervalMicroseconds;
					int64_t iDeviation = std::abs(iIntervalUs - iExpectedMicroseconds);
					mSmoothedJitterUs = iDeviation;
					mSmoothedJitterUs.Update();
				}
				mLastUpdateArrival = now;
				mStateFlags.Set(ClientStateFlags::kHasLastUpdateArrival);
			}
		}

		if (rMessage.uiSlotIndex >= std::ssize(mCoordSlots))
		{
			return;
		}
		CoordUpdateFlags_t actions = ClassifyCoordUpdate(rMessage.uiSlotIndex, rMessage.uiEpoch);
		if (!(actions & CoordUpdateFlags::kCommit))
		{
			return;
		}

		ScopedSuppressAllocationTracking suppress;

		ReceivedCoordUpdate update {};
		update.iTick = rMessage.iTick;
		update.sharedCrc = static_cast<common::crc_t>(rMessage.uiSharedCrc);

		if (rMessage.compressedPayload.iSize > 0)
		{
			int64_t iCount = game::NetworkSessionContract::DecompressStatusChanges(rMessage.compressedPayload.pData, rMessage.compressedPayload.iSize, mStatusChangeScratch.data(), kiMaxStatusChangesPerCell);
			// Heap: exact-size copy out of the reused 1024-cap decode scratch, so the buffered update carries no capacity slack
			update.statusChanges.assign(mStatusChangeScratch.begin(), mStatusChangeScratch.begin() + iCount);
		}

		// Heap: received updates vector grows each tick
		mReceivedCoordUpdates.at(rMessage.uiSlotIndex).push_back(std::move(update));
		if (actions & CoordUpdateFlags::kTrackTick)
		{
			TrackReceivedTick(rMessage.uiSlotIndex, rMessage.iTick);
		}
	};

	if (bProcessRtt)
	{
		NetworkMessages::ServerCoordUpdateMessage message {};
		if (NetworkMessages::Read(packetData, message))
		{
			receive(message);
		}
	}
	else
	{
		NetworkMessages::ServerCoordResendMessage message {};
		if (NetworkMessages::Read(packetData, message))
		{
			receive(message);
		}
	}
}

void Client::ServerDebugFrame(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerDebugFrameMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	int64_t iTick = message.iTick;
	GridCoord coord = message.coord;
	LOG(kNetwork, kError, "Client::ServerDebugFrame Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);
	ScopedLogIndent scopedLogIndent;

	// Heap: LZ4 decompresses debug frame; Frame allocated on heap
	ScopedSuppressAllocationTracking suppress;

	std::unique_ptr<game::Frame> pFrame = DecompressAndReadFrame(message.iUncompressedSize, message.compressedPayload);
	if (pFrame == nullptr)
	{
		LOG(kNetwork, kError, "Client::ServerDebugFrame LZ4 decompression or frame read failed Frame: {}", iTick);
		return;
	}

	mpReceivedDebugFrame = std::make_unique<ReceivedDebugFrame>();
	mpReceivedDebugFrame->pFrame = std::move(pFrame);
}

void Client::ServerConnectionResponse(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerConnectionResponseMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	bool bAccepted = message.uiAccepted != 0;

	if (bAccepted)
	{
		mStateFlags.Set(ClientStateFlags::kConnectionAccepted);

		if (message.bHasGuid)
		{
			mClientGuid = message.guid;
			LOG(kNetwork, kInfo, "Client GUID assigned: {} {}", mClientGuid.uiHigh, mClientGuid.uiLow);

			// Persist via the session-provided callback (keeps GUID disk I/O out of the transport layer)
			if (mpfnGuidAssigned != nullptr)
			{
				mpfnGuidAssigned(mClientGuid);
			}
		}

		// Seed smoothed pipeline RTT from game-layer handshake measurement so the value flows through the network sim
		int64_t iNowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		int64_t iRttUs = (miHelloSendTimeNs > 0) ? (iNowNs - miHelloSendTimeNs) / 1000 : 0;
		if (iRttUs > 0 && iRttUs < 60'000'000)
		{
			mSmoothedPipelineRttUs.Seed(iRttUs);
		}
		LOG(kNetwork, kDebug, "Client::ServerConnectionResponse Handshake RTT: {} us", iRttUs);
	}
	else
	{
		size_t iCopyLength = std::min(message.rejectionMessage.size(), sizeof(mpcRejectionReason) - 1);
		std::memcpy(mpcRejectionReason, message.rejectionMessage.data(), iCopyLength);
		mpcRejectionReason[iCopyLength] = '\0';
		LOG(kNetwork, kWarning, "Client::ServerConnectionResponse Rejected: {}", mpcRejectionReason);
	}
}

void Client::ServerSubscribeAccept(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerSubscribeAcceptMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint8_t uiSlotIndex = message.uiSlotIndex;
	uint16_t uiEpoch = message.uiEpoch;
	GridCoord coord = message.coord;

	if (uiSlotIndex == kuiSubscribeRejectSlot)
	{
		// Server rejected subscription (not adjacent / no free slot) — clear the kSubscribing placeholder
		ClearSubscribingPlaceholder(coord);
		LOG(kNetwork, kWarning, "Client::ServerSubscribeAccept Rejected Coord: ({},{})", coord.x, coord.y);
		return;
	}

	// Defensive (trust boundary: network input): a non-sentinel slot the client cannot host would
	// throw at mCoordSlots.at() below. Unsubscribe so a server-side slot cannot leak, then drop.
	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		LOG(kNetwork, kWarning, "Client::ServerSubscribeAccept Out-of-range, unsubscribing Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		NetworkMessages::ClientUnsubscribeMessage unsubscribe {.uiSlotIndex = uiSlotIndex, .uiEpoch = uiEpoch};
		NetworkMessages::Write(rWorkbuffer, unsubscribe);
		NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	SubscribeAcceptFlags_t actions = ClassifySubscribeAccept(uiSlotIndex, coord);

	if (actions & SubscribeAcceptFlags::kRejectGhost)
	{
		LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Ignoring Slot: {} Coord: ({},{}) SlotCoord: ({},{}) State: {}", uiSlotIndex, coord.x, coord.y, rSlot.coord.x, rSlot.coord.y, static_cast<int>(rSlot.eState));
		common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
		common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
		NetworkMessages::ClientUnsubscribeMessage unsubscribe {.uiSlotIndex = uiSlotIndex, .uiEpoch = uiEpoch};
		NetworkMessages::Write(rWorkbuffer, unsubscribe);
		NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
		RemoveCancelledSubscription(coord);
		LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept sent unsubscribe for ghost Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
		return;
	}

	if (actions & SubscribeAcceptFlags::kClearPlaceholder)
	{
		ClearSubscribingPlaceholder(coord);
	}

	if (actions & SubscribeAcceptFlags::kHealEpoch)
	{
		rSlot.ackState.uiEpoch = uiEpoch;
		if (RemoveCancelledSubscription(coord))
		{
			LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Healed then cancelled Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
			SendUnsubscribe(uiSlotIndex);
		}
		else
		{
			LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Healed active slot Slot: {} Coord: ({},{}) Epoch: {}", uiSlotIndex, coord.x, coord.y, uiEpoch);
		}
		return;
	}

	if (actions & SubscribeAcceptFlags::kCommitInit)
	{
		rSlot.coord = coord;
		rSlot.eState = CoordSubscriptionState::kWaitingFullState;
		rSlot.transitionStartTime = std::chrono::steady_clock::now();
		rSlot.ackState.iAckFloor = -1;
		rSlot.ackState.uiReceivedBitfieldLow = 0;
		rSlot.ackState.uiReceivedBitfieldHigh = 0;
		rSlot.ackState.uiEpoch = uiEpoch;

		if (RemoveCancelledSubscription(coord))
		{
			LOG(kNetwork, kVerbose, "Client::ServerSubscribeAccept Cancelled Slot: {} Coord: ({},{})", uiSlotIndex, coord.x, coord.y);
			SendUnsubscribe(uiSlotIndex);
		}
	}
}

void Client::ServerUnsubscribeAck(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerUnsubscribeAckMessage message {};
	if (!NetworkMessages::Read(packetData, message))
	{
		return;
	}

	uint8_t uiSlotIndex = message.uiSlotIndex;

	if (uiSlotIndex >= std::ssize(mCoordSlots))
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
	if (rSlot.eState != CoordSubscriptionState::kUnsubscribing)
	{
		return;
	}

	if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
	{
		NetworkSimulation::PurgeDelayedForSlot(mDelayedPackets, uiSlotIndex);
	}

	rSlot = {};
}

void Client::ServerLoadNotification(std::span<const uint8_t> packetData)
{
	NetworkMessages::ServerLoadNotificationMessage message {};
	if (NetworkMessages::Read(packetData, message))
	{
		mStateFlags.Set(ClientStateFlags::kLoadNotificationReceived);
	}
}

} // namespace engine

#endif // BT_CLIENT
