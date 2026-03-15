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
	kClientResyncRequest,       // Client requests full state re-download after desync recovery
	kServerSubscribeAccept,     // Server confirms subscription with assigned slot
	kServerUnsubscribeAck,      // Server confirms unsubscription
	kServerPlayerState,         // Server notifies client of player state change (spawn, frame change, death)
};

// Client request flags for spawn/respawn
enum class ClientRequestFlags : uint8_t
{
	kSpawnRequested   = 0x01,
	kRespawnRequested = 0x02,
};
using ClientRequestFlags_t = common::Flags<ClientRequestFlags>;

// Protocol constants
inline constexpr uint32_t kuiProtocolVersion = 1;
inline constexpr uint16_t kuiDefaultPort = 27015;
inline constexpr int64_t kiMaxResendFrames = 8;
inline constexpr int64_t kiMaxBufferedFrames = 256;
inline constexpr int64_t kiClockErrorDisconnectThreshold = 64;
inline constexpr int64_t kiMaxPacketSize = 64 * 1024;
inline constexpr int64_t kiMaxStatusChangesPerCell = 1024;

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr int64_t kiDiscoveryScanMs = 1500;

// Per-slot ACK tracking state (shared by client and server)
struct AckState
{
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfield = 0;
	uint16_t uiEpoch = 0;
};

} // namespace engine
