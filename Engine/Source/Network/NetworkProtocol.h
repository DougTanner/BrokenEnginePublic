#pragma once

namespace engine
{

// Packet types for client/server communication
enum class PacketType : uint8_t
{
	kServerCoordFullState,      // Per-coord full state (reliable, slot channel)
	kServerCoordStaticData,     // Per-coord static data sent once per subscription (reliable, slot channel)
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
	kClientResyncRequest,       // Client requests full state re-download after desync recovery
	kServerSubscribeAccept,     // Server confirms subscription with assigned slot
	kServerUnsubscribeAck,      // Server confirms unsubscription
	kClientPauseRequest,        // Client requests server pause/unpause (debug only)
	kClientTimespeedRequest,    // Client requests timescale change (debug only)
	kServerTimespeedUpdate,     // Server broadcasts current timescale to all clients
	kClientSaveRequest,         // Client requests server quicksave (debug only)
	kClientLoadRequest,         // Client requests server quickload (debug only)
	kServerLoadNotification,    // Server loaded a save, clients must reset state
	kClientReplayRecordRequest, // Client requests server replay record start/stop (debug only)
	kClientReplayPlaybackRequest, // Client requests server replay playback (debug only)
	kClientResetRequest,          // Client requests server reset (debug only)
	kGamePacketStart,             // All values >= this are game-layer packets forwarded as raw bytes
};

// Client request flags for spawn/respawn
enum class ClientRequestFlags : uint8_t
{
	kSpawnRequested   = 0x01,
	kRespawnRequested = 0x02,
};
using ClientRequestFlags_t = common::Flags<ClientRequestFlags>;

// Protocol constants
inline constexpr uint32_t kuiProtocolVersion = 3;
inline constexpr uint16_t kuiDefaultPort = 27015;
inline constexpr int64_t kiMaxResendFrames = 8;
inline constexpr int64_t kiMaxBufferedFrames = 256;
inline constexpr int64_t kiClockErrorDisconnectThreshold = 64;
inline constexpr int64_t kiClockErrorDisconnectConsecutiveFrames = 4;
inline constexpr int64_t kiMaxPacketSize = 64 * 1024;
inline constexpr int64_t kiMaxStatusChangesPerCell = 1024;

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr int64_t kiDiscoveryScanMs = 1500;

// Network buffer size for ACK bitfield and snapshot ring buffers (decoupled from physics tick rate)
inline constexpr int64_t kiNetworkBufferSize = 128;

// 128-bit client GUID for persistent identity across save/load
struct ClientGuid
{
	uint64_t uiHigh = 0;
	uint64_t uiLow = 0;

	bool IsEmpty() const { return uiHigh == 0 && uiLow == 0; }
	bool operator==(const ClientGuid&) const = default;
};

struct ClientGuidHash
{
	size_t operator()(const ClientGuid& rGuid) const
	{
		return std::hash<uint64_t>{}(rGuid.uiHigh) ^ (std::hash<uint64_t>{}(rGuid.uiLow) << 1);
	}
};

// Per-slot ACK tracking state (shared by client and server)
struct AckState
{
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfieldLow = 0;   // bits 0-63
	uint64_t uiReceivedBitfieldHigh = 0;  // bits 64-127
	uint16_t uiEpoch = 0;
};

} // namespace engine
