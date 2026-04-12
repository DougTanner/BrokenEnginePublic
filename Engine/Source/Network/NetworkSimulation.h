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

namespace NetworkSimulation
{

inline float Random01()
{
	static uint32_t suiState = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	suiState = suiState * 1103515245 + 12345;
	return static_cast<float>(suiState >> 16) / 65536.0f;
}

inline std::chrono::steady_clock::duration RandomOneWayDelay(const NetworkSimulationConfig& rConfig)
{
	int64_t iHalfMin = rConfig.iPingMinMs / 2;
	int64_t iHalfMax = rConfig.iPingMaxMs / 2;
	int64_t iDelayMs = iHalfMin + static_cast<int64_t>(Random01() * static_cast<float>(iHalfMax - iHalfMin));
	return std::chrono::milliseconds(iDelayMs);
}

inline bool ShouldDrop(const NetworkSimulationConfig& rConfig)
{
	static int64_t siConsecutiveDrops = 0;
	static constexpr int64_t kiMaxConsecutiveDrops = kiNetworkBufferSize / 2;

	bool bDrop = false;
	if (siConsecutiveDrops > 0)
	{
		bDrop = siConsecutiveDrops < kiMaxConsecutiveDrops && Random01() < 0.5f;
	}
	else
	{
		bDrop = Random01() * 100.0f < rConfig.fPacketLossPercent;
	}

	siConsecutiveDrops = bDrop ? siConsecutiveDrops + 1 : 0;
	return bDrop;
}

// Enqueue a received unreliable packet into the delay queue, or drop it.
// Reliable packets are passed through immediately via handleReliable.
template <typename FnHandleReliable>
inline void EnqueueOrDrop(std::deque<DelayedPacket>& rDelayedPackets, const NetworkSimulationConfig& rSimConfig, ENetEvent& rEvent, FnHandleReliable handleReliable)
{
	bool bUnreliable = NetworkManager::IsUnreliableChannel(rEvent.channelID);
	if (bUnreliable)
	{
		if (ShouldDrop(rSimConfig))
		{
			LOG(kNetwork, kVerbose, "NetworkSimulation dropped unreliable packet channel: {} size: {}", rEvent.channelID, rEvent.packet->dataLength);
			enet_packet_destroy(rEvent.packet);
			return;
		}
		ScopedSuppressAllocationTracking scopedSuppressAllocationTracking;
		// Heap: delay queue copies packet data for deferred processing
		DelayedPacket delayed {};
		delayed.releaseTime = std::chrono::steady_clock::now() + RandomOneWayDelay(rSimConfig);
		delayed.data.assign(rEvent.packet->data, rEvent.packet->data + rEvent.packet->dataLength);
		delayed.pPeer = rEvent.peer;
		delayed.uiChannelId = rEvent.channelID;
		auto insertPos = std::lower_bound(rDelayedPackets.begin(), rDelayedPackets.end(), delayed,
		[](const DelayedPacket& rA, const DelayedPacket& rB) { return rA.releaseTime < rB.releaseTime; });
		rDelayedPackets.insert(insertPos, std::move(delayed));
		enet_packet_destroy(rEvent.packet);
	}
	else
	{
		handleReliable(rEvent);
		enet_packet_destroy(rEvent.packet);
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

} // namespace NetworkSimulation

} // namespace engine
