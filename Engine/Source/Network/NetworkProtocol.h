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
	kServerLoadNotification,    // Server loaded a save, clients must reset state
	kGamePacketStart,           // All values >= this are game-layer packets forwarded as raw bytes
};

// Client request flags for spawn/respawn
enum class ClientRequestFlags : uint8_t
{
	kSpawnRequested   = 0x01,
	kRespawnRequested = 0x02,
};
using ClientRequestFlags_t = common::Flags<ClientRequestFlags>;

inline constexpr const char* PacketTypeName(PacketType eType)
{
	switch (eType)
	{
		case PacketType::kServerCoordFullState:         return "kServerCoordFullState";
		case PacketType::kServerCoordStaticData:        return "kServerCoordStaticData";
		case PacketType::kServerCoordUpdate:            return "kServerCoordUpdate";
		case PacketType::kServerCoordResend:            return "kServerCoordResend";
		case PacketType::kClientSpawnRequest:           return "kClientSpawnRequest";
		case PacketType::kServerDebugFrame:             return "kServerDebugFrame";
		case PacketType::kClientDesyncReport:           return "kClientDesyncReport";
		case PacketType::kClientAckStream:              return "kClientAckStream";
		case PacketType::kClientDebugFrameRequest:      return "kClientDebugFrameRequest";
		case PacketType::kClientHello:                  return "kClientHello";
		case PacketType::kServerConnectionResponse:     return "kServerConnectionResponse";
		case PacketType::kClientSubscribe:              return "kClientSubscribe";
		case PacketType::kClientUnsubscribe:            return "kClientUnsubscribe";
		case PacketType::kClientResyncRequest:          return "kClientResyncRequest";
		case PacketType::kServerSubscribeAccept:        return "kServerSubscribeAccept";
		case PacketType::kServerUnsubscribeAck:         return "kServerUnsubscribeAck";
		case PacketType::kServerLoadNotification:       return "kServerLoadNotification";
		case PacketType::kGamePacketStart:              return "kGamePacketStart";
	}
	return "Unknown";
}

// Protocol constants
inline constexpr uint32_t kuiProtocolVersion = 5;
inline constexpr uint8_t kuiSubscribeRejectSlot = 0xFF; // Sentinel slot in kServerSubscribeAccept: server rejected the subscribe (not adjacent / no free slot)
inline constexpr uint16_t kuiDefaultPort = 27015;
inline constexpr int64_t kiMaxResendFrames = 8;
inline constexpr int64_t kiFloorStallLogThreshold = 15;
inline constexpr int64_t kiMaxBufferedFrames = 256;
inline constexpr int64_t kiClockErrorDisconnectThreshold = 64;
inline constexpr int64_t kiClockErrorDisconnectConsecutiveFrames = 4;
// Fixed jitter safety buffer added on top of measured jitter when computing miCurrentTargetBehind.
// 125ms = 4 ticks at 32Hz; preserved in wall-clock terms if the tick rate ever changes.
inline constexpr int64_t kiJitterSafetyUs = 125'000;
// Slack between the clock-servo target (latestServerTick - miCurrentTargetBehind) and the hard sim
// ceiling in GameBase::ClientUpdate. The servo steers toward the bare target so the sim never rests
// against the ceiling; the slack absorbs per-packet arrival jitter and the 2-tick targetBehind
// hysteresis step without stalling the sim. Must stay below ComputeClockCorrectionNs's |error| >= 4
// aggressive-correction threshold so steady state never triggers it.
inline constexpr int64_t kiSimCeilingSlackTicks = 3;
inline constexpr int64_t kiMaxPacketSize = 64 * 1024;
inline constexpr int64_t kiMaxStatusChangesPerCell = 1024;
// Hard ceiling on a decompressed full-frame payload. Trust boundary: the wire-controlled uncompressed-size prefix in
// kServerCoordFullState / kServerDebugFrame drives the decompress-buffer allocation, so a hostile prefix is clamped to
// a bounded alloc before the std::string reserve (the frame reader validates element counts further). No legit
// single-cell frame approaches 64 MiB.
inline constexpr int64_t kiMaxUncompressedFrameBytes = 64 * 1024 * 1024;

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr std::chrono::milliseconds kDiscoveryScanDuration {1500};

// Network buffer size for ACK bitfield and snapshot ring buffers (decoupled from physics tick rate)
inline constexpr int64_t kiNetworkBufferSize = 128;

// 128-bit client GUID for persistent identity across save/load
struct ClientGuid
{
	// On-disk ClientGuid.bin header version (persist/load route through WriteVersionedFile/ReadVersionedFile).
	// v2 migrated off the legacy hand-rolled v1/size-0 header to the shared version+size convention; v1 files reset once.
	static constexpr int64_t kiVersion = 2;

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
