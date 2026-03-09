#pragma once

namespace engine
{

// Packet types for client/server communication
enum class PacketType : uint8_t
{
	kServerAssignPlayer,
	kServerCoordFullState,      // Per-coord full state (reliable, slot channel)
	kServerCoordUpdate,         // Per-coord delta update (unreliable, slot channel)
	kServerCoordResend,         // Per-coord re-sent frame (unreliable, slot channel)
	kClientSpawnRequest,
	kServerDebugFrame,
	kClientDesyncReport,
	kClientAckStream,
	kClientDebugFrameRequest,
	kClientHello,
	kServerConnectionResponse,
	kClientSubscribe,           // Client requests subscription to a GridCoord
	kClientUnsubscribe,         // Client releases a coord slot
	kServerSubscribeAccept,     // Server confirms subscription with assigned slot
	kServerUnsubscribeAck,      // Server confirms unsubscription
	kServerPlayerState,         // Server notifies client of player state change (spawn, frame change, death)
};

// Player state transitions sent via kServerPlayerState
enum class PlayerStateType : uint8_t
{
	kSpawned,
	kChangedFrame,
	kDied,
};

// Client request flags for spawn/respawn
enum class ClientRequestFlags : uint8_t
{
	kSpawnRequested   = 0x01,
	kRespawnRequested = 0x02,
};
using ClientRequestFlags_t = common::Flags<ClientRequestFlags>;

// Protocol constants
inline constexpr uint16_t kuiDefaultPort = 27015;
inline constexpr int64_t kiMaxResendFrames = 8;
inline constexpr int64_t kiMaxBufferedFrames = 256;
inline constexpr int64_t kiMaxPacketSize = 64 * 1024;
inline constexpr int64_t kiMaxStatusChangesPerCell = 1024;

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr int64_t kiDiscoveryScanMs = 1500;

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
	float fPacketLossPercent;
	int64_t iPingMinMs;
	int64_t iPingMaxMs;
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
	static uint32_t suiState = 2147483647;
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
	static constexpr int64_t kiMaxConsecutiveDrops = kiTickRate / 2;

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

} // namespace NetworkSimulation

} // namespace engine
