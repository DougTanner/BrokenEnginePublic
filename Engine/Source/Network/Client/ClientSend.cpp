#include "Pch.h"

#include "Network/Client/Client.h"

#if defined(BT_CLIENT)

namespace engine
{

bool Client::SendAck()
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return false;
	}

	// Tick-rate-locked cadence: skip if less than one sim-tick interval has elapsed since the last ack,
	// so ack packet rate is decoupled from render framerate. Interval derived from kiTickRate (never a
	// hardcoded Hz literal) so a future tick-rate change carries the ack cadence with it. Not gated on
	// sim advancement -- a paused/stalled sim must still ack so server resends/RTT stay alive.
	constexpr std::chrono::nanoseconds kAckInterval {1'000'000'000 / kiTickRate};
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now - mLastAckSendTime < kAckInterval)
	{
		return false;
	}
	mLastAckSendTime = now;

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	// Per-slot ACK state for proactive re-sends
	NetworkMessages::AckStreamEntry entries[NetworkManager::kiMaxEnetCoordSlots] {};
	uint8_t uiAckSlotCount = 0;
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kActive)
		{
			entries[uiAckSlotCount] = {
				.uiSlotIndex = static_cast<uint8_t>(i),
				.uiEpoch = mCoordSlots.at(i).ackState.uiEpoch,
				.iAckFloor = mCoordSlots.at(i).ackState.iAckFloor,
				.uiReceivedBitfieldLow = mCoordSlots.at(i).ackState.uiReceivedBitfieldLow,
				.uiReceivedBitfieldHigh = mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh,
			};
			++uiAckSlotCount;
		}
	}

	// Pipeline RTT: embed client timestamp for server to echo back
	int64_t iTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	NetworkMessages::ClientAckStreamMessage message {
		.uiSlotCount = uiAckSlotCount,
		.pEntries = entries,
		.iEntryCapacity = NetworkManager::kiMaxEnetCoordSlots,
		.iTimestampNs = iTimestampNs,
	};
	NetworkMessages::Write(rWorkbuffer, message);

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelUnreliable, rWorkbuffer, 0);
	return true;
}

void Client::SendDesyncReport(int64_t iTick, GridCoord coord, common::crc_t expected, common::crc_t actual)
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return;
	}

	char pcExpected[20] {};
	char pcActual[20] {};
	LOG(kNetwork, kError, "Client::SendDesyncReport Frame: {} Grid: ({},{}) Expected: {} Actual: {}", iTick, coord.x, coord.y, common::ToHex(std::span(pcExpected), expected), common::ToHex(std::span(pcActual), actual));

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ClientDesyncReportMessage message {
		.iTick = iTick,
		.coord = coord,
		.uiExpectedCrc = static_cast<uint64_t>(expected),
		.uiActualCrc = static_cast<uint64_t>(actual),
	};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Client::SendDebugFrameRequest(int64_t iTick, GridCoord coord)
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return;
	}

	LOG(kNetwork, kError, "Client::SendDebugFrameRequest Frame: {} Grid: ({},{})", iTick, coord.x, coord.y);

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ClientDebugFrameRequestMessage message {.iTick = iTick, .coord = coord};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

bool Client::SendSubscribe(GridCoord coord)
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return false;
	}

	// Mark a local slot as kSubscribing
	bool bFoundSlot = false;
	for (int64_t i = 0; i < std::ssize(mCoordSlots); ++i)
	{
		if (mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribed || mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
		{
			// Purge delayed packets for the old slot's channels before reuse
			if constexpr (keNetworkSimulation != engine::NetworkSimulationLevel::kDisabled)
			{
				if (mCoordSlots.at(i).eState == CoordSubscriptionState::kUnsubscribing)
				{
					NetworkSimulation::PurgeDelayedForSlot(mDelayedPackets, i);
				}
			}

			mCoordSlots.at(i).coord = coord;
			mCoordSlots.at(i).eState = CoordSubscriptionState::kSubscribing;
			mCoordSlots.at(i).transitionStartTime = std::chrono::steady_clock::now();
			mCoordSlots.at(i).ackState.iAckFloor = -1;
			mCoordSlots.at(i).ackState.uiReceivedBitfieldLow = 0;
			mCoordSlots.at(i).ackState.uiReceivedBitfieldHigh = 0;
			bFoundSlot = true;
			break;
		}
	}
	if (!bFoundSlot)
	{
		return false;
	}

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ClientSubscribeMessage message {.coord = coord};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);

	return true;
}

void Client::SendUnsubscribe(int64_t iSlot)
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return;
	}

	ClientCoordSlot& rSlot = mCoordSlots.at(iSlot);
	rSlot.eState = CoordSubscriptionState::kUnsubscribing;
	rSlot.transitionStartTime = std::chrono::steady_clock::now();
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ClientUnsubscribeMessage message {
		.uiSlotIndex = static_cast<uint8_t>(iSlot),
		.uiEpoch = rSlot.ackState.uiEpoch,
	};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Client::SendResyncRequest()
{
	if (!(mStateFlags & ClientStateFlags::kConnected) || mpServerPeer == nullptr)
	{
		return;
	}

	LOG(kNetwork, kError, "Client::SendResyncRequest");

	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();
	NetworkMessages::ClientResyncRequestMessage message {};
	NetworkMessages::Write(rWorkbuffer, message);
	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

void Client::SendHello()
{
	common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
	common::ScopedWorkbufferArena scopedWorkbufferArena = rWorkbuffer.Push();

	NetworkMessages::ClientHelloMessage message {
		.uiProtocolVersion = kuiProtocolVersion,
		.iFrameVersion = game::NetworkSessionContract::GetFrameVersion(),
		.packIntegrityToken = gpFileManager->GetPackIntegrityToken(),
		.buildConfig = kpcBuildConfigName,
		.guid = mClientGuid,
		.bHasGuid = true,
	};
	NetworkMessages::Write(rWorkbuffer, message);

	miHelloSendTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

	NetworkManager::SendPacket(mpServerPeer, NetworkManager::kuiChannelReliable, rWorkbuffer, ENET_PACKET_FLAG_RELIABLE);
}

} // namespace engine

#endif // BT_CLIENT
