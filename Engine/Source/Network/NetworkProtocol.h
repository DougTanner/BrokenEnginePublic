#pragma once

namespace engine
{

// Packet types for client/server communication
enum class PacketType : uint8_t
{
	kServerAssignPlayer,
	kServerFullState,
	kServerUpdateStream,
	kServerResendStream,
	kClientSpawnRequest,
	kServerDebugFrame,
	kClientDesyncReport,
	kClientInputStream,
	kClientDebugFrameRequest,
	kClientHello,
	kServerConnectionResponse,
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
inline constexpr int64_t kiMaxMissingFrames = 128;

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr int64_t kiDiscoveryScanMs = 1500;

// Network simulation constants (applied per-direction, so half-ping delay on each side)
inline constexpr float kfSimulatedPacketLossPercent = 2.5f;
inline constexpr int64_t kiSimulatedPingMinMs = 300;
inline constexpr int64_t kiSimulatedPingMaxMs = 500;

struct DelayedPacket
{
	std::chrono::steady_clock::time_point releaseTime;
	std::vector<uint8_t> data;
	ENetPeer* pPeer = nullptr;
};

namespace NetworkSimulation
{

inline float Random01()
{
	static uint32_t suiState = 2147483647;
	suiState = suiState * 1103515245 + 12345;
	return static_cast<float>(suiState >> 16) / 65536.0f;
}

inline std::chrono::steady_clock::duration RandomOneWayDelay()
{
	int64_t iHalfMin = kiSimulatedPingMinMs / 2;
	int64_t iHalfMax = kiSimulatedPingMaxMs / 2;
	int64_t iDelayMs = iHalfMin + static_cast<int64_t>(Random01() * static_cast<float>(iHalfMax - iHalfMin));
	return std::chrono::milliseconds(iDelayMs);
}

inline bool ShouldDrop()
{
	static int64_t siConsecutiveDrops = 0;
	static constexpr int64_t kiMaxConsecutiveDrops = kiMaxMissingFrames / 2;

	bool bDrop = false;
	if (siConsecutiveDrops > 0)
	{
		bDrop = siConsecutiveDrops < kiMaxConsecutiveDrops && Random01() < 0.5f;
	}
	else
	{
		bDrop = Random01() * 100.0f < kfSimulatedPacketLossPercent;
	}

	siConsecutiveDrops = bDrop ? siConsecutiveDrops + 1 : 0;
	return bDrop;
}

} // namespace NetworkSimulation

} // namespace engine
