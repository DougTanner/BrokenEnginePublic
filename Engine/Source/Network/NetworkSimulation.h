#pragma once

namespace engine
{

// Network simulation levels for testing different real-world latency scenarios (East Coast server)
enum class NetworkSimulationLevel : uint8_t
{
	kDisabled,
	kEastCoast,    // East Coast to East Coast
	kWestCoast,    // West Coast to East Coast
	kEurope,       // Europe to East Coast
	kSouthAmerica, // South America to East Coast
	kChina,        // China (behind firewall) to East Coast
};

struct NetworkSimulationConfig
{
	float fPacketLossPercent = 0.0f;
	int64_t iPingMinMs = 0;
	int64_t iPingMaxMs = 0;
};

// Applied per-direction, so half-ping delay on each side
inline constexpr NetworkSimulationConfig GetNetworkSimulationConfig(NetworkSimulationLevel eLevel)
{
	switch (eLevel)
	{
		case NetworkSimulationLevel::kEastCoast:    return {0.5f,  20,  40};
		case NetworkSimulationLevel::kWestCoast:    return {1.0f,  60,  90};
		case NetworkSimulationLevel::kEurope:       return {1.5f,  80, 130};
		case NetworkSimulationLevel::kSouthAmerica: return {2.0f, 120, 200};
		case NetworkSimulationLevel::kChina:        return {2.5f, 300, 500};
		default:                                    return {0.0f,   0,   0};
	}
}

inline constexpr std::string_view GetNetworkSimulationName(NetworkSimulationLevel eLevel)
{
	switch (eLevel)
	{
		case NetworkSimulationLevel::kEastCoast:    return "EastCoast";
		case NetworkSimulationLevel::kWestCoast:    return "WestCoast";
		case NetworkSimulationLevel::kEurope:       return "Europe";
		case NetworkSimulationLevel::kSouthAmerica: return "SouthAmerica";
		case NetworkSimulationLevel::kChina:        return "China";
		default:                                    return "Off";
	}
}

struct NetworkSimulationBounds
{
	int64_t iCrcMin = 0;
	int64_t iAssumedMax = 0;
	int64_t iFastReplayMax = 0;
	int64_t iStatusReplayMax = 0;
	int64_t iKnockOnReplayMax = 0;
};

inline constexpr NetworkSimulationBounds GetNetworkSimulationBounds(NetworkSimulationLevel eLevel)
{
	switch (eLevel)
	{
		case NetworkSimulationLevel::kDisabled:     return {62,  2,  2,  2,  2};
		case NetworkSimulationLevel::kEastCoast:    return {50,  8,  6, 10, 10};
		case NetworkSimulationLevel::kWestCoast:    return {40, 20, 20, 20, 24};
		case NetworkSimulationLevel::kEurope:       return {32, 32, 32, 30, 36};
		case NetworkSimulationLevel::kSouthAmerica: return {24, 50, 50, 44, 50};
		case NetworkSimulationLevel::kChina:        return {12, 90, 600, 70, 80};
		default:                                    return {62,  2,  2,  2,  2};
	}
}

struct DelayedPacket
{
	std::chrono::steady_clock::time_point releaseTime;
	std::vector<uint8_t> data;
	ENetPeer* pPeer = nullptr;
	uint8_t uiChannelId = 0;
};

// Owned per-peer (one Client or Server per process) so simulated latency/loss is reproducible across runs.
struct NetworkSimulationState
{
	static constexpr uint32_t kuiSeed = 0x9E3779B9u; // Constant seed (was wall-clock) so sim runs reproduce.
	uint32_t uiRandomState = kuiSeed;
	int64_t iConsecutiveDrops[NetworkManager::kuiChannelCount] {};
	std::chrono::steady_clock::time_point channelReleaseTimes[NetworkManager::kuiChannelCount] {};
	int64_t iCoordDropCounts[NetworkManager::kiMaxEnetCoordSlots] {};
	int64_t iControlDropCount = 0;
};

namespace NetworkSimulation
{

inline float Random01(NetworkSimulationState& rState)
{
	rState.uiRandomState = rState.uiRandomState * 1103515245 + 12345;
	return static_cast<float>(rState.uiRandomState >> 16) / 65536.0f;
}

inline std::chrono::steady_clock::duration RandomOneWayDelay(NetworkSimulationState& rState, const NetworkSimulationConfig& rConfig)
{
	int64_t iHalfMin = rConfig.iPingMinMs / 2;
	int64_t iHalfMax = rConfig.iPingMaxMs / 2;
	int64_t iDelayMs = iHalfMin + static_cast<int64_t>(Random01(rState) * static_cast<float>(iHalfMax - iHalfMin));
	return std::chrono::milliseconds(iDelayMs);
}

struct DropResult
{
	bool bDrop = false;
	int64_t iConsecutive = 0;
};

inline DropResult ShouldDrop(NetworkSimulationState& rState, const NetworkSimulationConfig& rConfig, uint8_t uiChannel)
{
	static constexpr int64_t kiMaxConsecutiveDrops = kiNetworkBufferSize / 2;

	int64_t& riDrops = rState.iConsecutiveDrops[uiChannel];
	bool bDrop = false;
	if (riDrops > 0)
	{
		bDrop = riDrops < kiMaxConsecutiveDrops && Random01(rState) < 0.5f;
	}
	else
	{
		bDrop = Random01(rState) * 100.0f < rConfig.fPacketLossPercent;
	}

	riDrops = bDrop ? riDrops + 1 : 0;
	return {bDrop, riDrops};
}

// Enqueue a received packet into the delay queue. Unreliable packets may be dropped per the sim config;
// reliable packets are also enqueued (never dropped) in per-channel monotonic FIFO release order.
inline void EnqueueOrDrop(std::deque<DelayedPacket>& rDelayedPackets, NetworkSimulationState& rState, const NetworkSimulationConfig& rSimConfig, ENetEvent& rEvent)
{
	// upper_bound (not lower_bound) inserts after equal-key elements, keeping same-channel FIFO stable
	// when the reliable monotonic-release clamp produces identical release times.
	auto enqueueDelayed = [&rDelayedPackets, &rEvent](std::chrono::steady_clock::time_point releaseTime)
	{
		ScopedSuppressAllocationTracking suppress;
		// Heap: delay queue copies packet data for deferred processing
		DelayedPacket delayed {};
		delayed.releaseTime = releaseTime;
		delayed.data.assign(rEvent.packet->data, rEvent.packet->data + rEvent.packet->dataLength);
		delayed.pPeer = rEvent.peer;
		delayed.uiChannelId = rEvent.channelID;
		auto insertPos = std::upper_bound(rDelayedPackets.begin(), rDelayedPackets.end(), delayed, [](const DelayedPacket& rA, const DelayedPacket& rB)
		{
			return rA.releaseTime < rB.releaseTime;
		});
		rDelayedPackets.insert(insertPos, std::move(delayed));
		enet_packet_destroy(rEvent.packet);
	};

	bool bUnreliable = NetworkManager::IsUnreliableChannel(rEvent.channelID);
	if (bUnreliable)
	{
		DropResult dropResult = ShouldDrop(rState, rSimConfig, rEvent.channelID);
		if (dropResult.bDrop)
		{
			if (NetworkManager::IsCoordChannel(rEvent.channelID))
			{
				int64_t iSlot = NetworkManager::ChannelToSlot(rEvent.channelID);
				uint8_t uiPacketType = (rEvent.packet->dataLength > 0) ? rEvent.packet->data[0] : 0;
				int64_t iTick = NetworkMessages::GetCoordUpdateTickOrZero(std::span<const uint8_t>(rEvent.packet->data, rEvent.packet->dataLength));
				++rState.iCoordDropCounts[iSlot];
				LOG(kNetwork, kVerbose, "NetworkSimulation::Dropped Coord Slot: {} Tick: {} Type: {} Size: {} TotalDrops: {} Consecutive: {}", iSlot, iTick, PacketTypeName(static_cast<PacketType>(uiPacketType)), rEvent.packet->dataLength, rState.iCoordDropCounts[iSlot], dropResult.iConsecutive);
			}
			else
			{
				++rState.iControlDropCount;
				LOG(kNetwork, kVerbose, "NetworkSimulation::Dropped Control Channel: {} Size: {} TotalDrops: {} Consecutive: {}", rEvent.channelID, rEvent.packet->dataLength, rState.iControlDropCount, dropResult.iConsecutive);
			}
			enet_packet_destroy(rEvent.packet);
			return;
		}
		enqueueDelayed(std::chrono::steady_clock::now() + RandomOneWayDelay(rState, rSimConfig));
	}
	else
	{
		std::chrono::steady_clock::time_point releaseTime = std::max(std::chrono::steady_clock::now() + RandomOneWayDelay(rState, rSimConfig), rState.channelReleaseTimes[rEvent.channelID]);
		rState.channelReleaseTimes[rEvent.channelID] = releaseTime;
		enqueueDelayed(releaseTime);
	}
}

// Fast-forward (time multiply > 1) bypasses the delay queue; otherwise enqueue-or-drop per the sim config.
template <typename FnReceive>
inline void DispatchOrEnqueue(std::deque<DelayedPacket>& rDelayedPackets, NetworkSimulationState& rState, const NetworkSimulationConfig& rSimConfig, bool bFastForward, ENetEvent& rEvent, FnReceive fnReceive)
{
	if (bFastForward)
	{
		fnReceive(rEvent);
		enet_packet_destroy(rEvent.packet);
	}
	else
	{
		EnqueueOrDrop(rDelayedPackets, rState, rSimConfig, rEvent);
	}
}

// Process delayed packets whose release time has passed.
template <typename FnHandlePacket>
inline void ProcessDelayed(std::deque<DelayedPacket>& rDelayedPackets, FnHandlePacket handlePacket)
{
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	while (!rDelayedPackets.empty() && rDelayedPackets.front().releaseTime <= now)
	{
		handlePacket(rDelayedPackets.front());
		rDelayedPackets.pop_front();
	}
}

// Flush all delayed packets immediately, ignoring release times.
template <typename FnHandlePacket>
inline void FlushDelayed(std::deque<DelayedPacket>& rDelayedPackets, FnHandlePacket handlePacket)
{
	while (!rDelayedPackets.empty())
	{
		handlePacket(rDelayedPackets.front());
		rDelayedPackets.pop_front();
	}
}

// Fast-forward flushes the whole delay queue immediately; otherwise releases packets whose time has passed.
template <typename FnHandlePacket>
inline void ProcessOrFlush(std::deque<DelayedPacket>& rDelayedPackets, bool bFastForward, FnHandlePacket handlePacket)
{
	if (bFastForward)
	{
		FlushDelayed(rDelayedPackets, handlePacket);
	}
	else
	{
		ProcessDelayed(rDelayedPackets, handlePacket);
	}
}

// Drop delayed packets queued on a coord slot's channels (called on slot reuse / unsubscribe-ack).
inline void PurgeDelayedForSlot(std::deque<DelayedPacket>& rDelayedPackets, int64_t iSlot)
{
	std::erase_if(rDelayedPackets, [iSlot](const DelayedPacket& rPacket)
	{
		return NetworkManager::IsCoordChannel(rPacket.uiChannelId) && NetworkManager::ChannelToSlot(rPacket.uiChannelId) == iSlot;
	});
}

} // namespace NetworkSimulation

} // namespace engine
