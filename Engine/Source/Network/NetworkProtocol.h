#pragma once

namespace engine
{

struct NetworkTimeState
{
	bool bFastForward = false;
	int64_t iExpectedUpdateIntervalMicroseconds = 0;
	int64_t iExpectedUpdatesPerSecond = 0;
};

// Packet types for client/server communication
enum class PacketType : uint8_t
{
	kServerCoordFullState,      // Per-coord full state (reliable, slot channel)
	kServerCoordStaticData,     // Per-coord static data sent once per subscription (reliable, slot channel)
	kServerCoordUpdate,         // Per-coord delta update (unreliable, slot channel)
	kServerCoordResend,         // Per-coord re-sent frame (unreliable, slot channel)
	kServerDebugFrame,
	kClientDesyncReport,
	kClientAckStream,
	kClientDebugFrameRequest,
	kClientHello,
	kServerConnectionResponse,
	kClientSubscribe,           // Client requests subscription to a GridCoord
	kClientUnsubscribe,         // Client releases a coord slot at the observed epoch
	kClientResyncRequest,       // Client requests full state re-download after desync recovery
	kServerSubscribeAccept,     // Server confirms subscription with assigned slot
	kServerUnsubscribeAck,      // Server confirms unsubscription
	kServerLoadNotification,    // Server loaded a save, clients must reset state
	kGamePacketStart,           // All values >= this are game-layer packets forwarded as raw bytes
};

inline constexpr const char* PacketTypeName(PacketType eType)
{
	switch (eType)
	{
		case PacketType::kServerCoordFullState:         return "kServerCoordFullState";
		case PacketType::kServerCoordStaticData:        return "kServerCoordStaticData";
		case PacketType::kServerCoordUpdate:            return "kServerCoordUpdate";
		case PacketType::kServerCoordResend:            return "kServerCoordResend";
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
inline constexpr uint32_t kuiProtocolVersion = 10;
inline constexpr uint8_t kuiSubscribeRejectSlot = 0xFF; // Sentinel slot in kServerSubscribeAccept: server rejected the subscribe (not adjacent / no free slot)
inline constexpr uint16_t kuiDefaultPort = 27015;
inline constexpr int64_t kiMaxResendFrames = 8;
inline constexpr int64_t kiFloorStallLogThreshold = 15;
inline constexpr int64_t kiMaxBufferedFrames = 256;
inline constexpr int64_t kiClockSnapThreshold = 28; // |clockError| >= this (ticks) hard-snaps miTickCounter to the servo target; see Network.md
// Fixed jitter safety buffer added on top of 3x measured jitter when computing miCurrentTargetBehind.
// 109.375ms = 3.5 ticks at 32Hz: deliberately off the tick boundary so the zero-jitter floor rounds up to
// a true 4 ticks instead of 5. Measured jitter counts 3x because mSmoothedJitterUs is the mean
// |interarrival deviation|, which understates the one-way arrival tail the buffer must cover (tail
// half-width is roughly 1.5x that mean). Preserved in wall-clock terms if the tick rate ever changes.
inline constexpr int64_t kiJitterSafetyUs = 109'375;
// Slack between the clock-servo target (latestServerTick - miCurrentTargetBehind) and the hard sim
// ceiling in GameBase::ClientUpdate. The servo steers toward the bare target so the sim never rests
// against the ceiling; the slack absorbs per-packet arrival jitter and the single-tick targetBehind
// raise step in EvaluateClock (which lowers the ceiling by one tick per call) without stalling the sim.
// Must stay below EvaluateClock's |error| >= 4 aggressive-correction threshold so steady state never
// triggers it.
inline constexpr int64_t kiSimCeilingSlackTicks = 3;
inline constexpr int64_t kiMaxPacketSize = 64 * 1024;
inline constexpr int64_t kiMaxStatusChangesPerCell = 1024;
// Hard ceiling on a decompressed full-frame payload. Trust boundary: the wire-controlled uncompressed-size prefix in
// kServerCoordFullState / kServerDebugFrame drives the decompress-buffer allocation, so a hostile prefix is clamped to
// a bounded alloc before the std::string reserve (the frame reader validates element counts further). No legit
// single-cell frame approaches 64 MiB.
inline constexpr int64_t kiMaxUncompressedFrameBytes = 64 * 1024 * 1024;

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

} // namespace engine

#include "Network/NetworkMessages.h"

namespace engine
{

// --- Client -> Server message contract --------------------------------------------------------------
// Declarative per-packet limits checked once at the server dispatch choke point (Server::Receive).
// Covers only engine packet types below kGamePacketStart; game-range types are contract-checked at
// parse via GetGamePacketContract. See Documents/Architecture/Network.md "Client -> Server Contract".

// The 64 mirrors NetworkManager::kiMaxEnetCoordSlots; a static_assert tying it to the transport
// ceiling lives in Server.cpp because NetworkProtocol.h cannot include NetworkManager.h.
inline constexpr int64_t kiMaxAckStreamPacketSize = NetworkMessages::ClientAckStreamMessage::GetSize(NetworkMessages::ClientAckStreamMessage::kiMaxSlotCount);

// Global per-client packet budget per update window (approx one sim tick; an update polls twice and both
// polls share the window). With the tick-rate-locked ack throttle a legit client sends ~1 ack + rare
// requests per tick; 256 tolerates a multi-second server stall delivering many wall-clock seconds of
// tick-rate acks in one update (e.g. 4 s ~= 128 acks at 32 Hz) plus request bursts. Overflow counts
// violations -- only hostile floods reach it.
inline constexpr int64_t kiMaxClientPacketsPerTick = 256;

// Global per-client inbound byte budget per update window. >10x legitimate steady state; caps hostile
// parse work at ~2 MiB/s/client.
inline constexpr int64_t kiMaxClientInboundBytesPerTick = 64 * 1024;

// Lifetime contract-violation count that forces a disconnect. Never reset -- legitimate clients produce
// zero violations; tolerates rare in-flight UDP corruption.
inline constexpr int64_t kiContractViolationDisconnectCount = 32;

// Per-client-packet contract row. Sizes are the full packet including the type byte.
struct ClientPacketContract
{
	int64_t iMinSize = 0;
	int64_t iMaxSize = 0;              // 0 = not client-sendable (sentinel)
	int64_t iMaxPerTick = 0;
	bool bRequiresHandshake = true;
	bool bOverCapCountsViolation = true;
};

// Contract for engine packet types (< kGamePacketStart) only. Game-range types bypass this at
// Server::Receive and are contract-checked at parse via GetGamePacketContract. Default row is the
// not-client-sendable sentinel (server->client and unknown engine types).
inline constexpr ClientPacketContract GetClientPacketContract(PacketType eType)
{
	switch (eType)
	{
		case PacketType::kClientAckStream:         return {NetworkMessages::ClientAckStreamMessage::kiFixedSize, kiMaxAckStreamPacketSize, 128, true, false}; // over-cap is a silent multi-tick-poll burst safety drop
		case PacketType::kClientDesyncReport:      return {NetworkMessages::ClientDesyncReportMessage::kiFixedSize, NetworkMessages::ClientDesyncReportMessage::kiFixedSize, 8};
		case PacketType::kClientDebugFrameRequest: return {NetworkMessages::ClientDebugFrameRequestMessage::kiFixedSize, NetworkMessages::ClientDebugFrameRequestMessage::kiFixedSize, 8};
		case PacketType::kClientHello:             return {.iMinSize = NetworkMessages::ClientHelloMessage::kiMinSize, .iMaxSize = NetworkMessages::ClientHelloMessage::kiMaxSize, .iMaxPerTick = 4, .bRequiresHandshake = false}; // pre-handshake by definition
		case PacketType::kClientSubscribe:         return {NetworkMessages::ClientSubscribeMessage::kiFixedSize, NetworkMessages::ClientSubscribeMessage::kiFixedSize, 64};
		case PacketType::kClientUnsubscribe:       return {NetworkMessages::ClientUnsubscribeMessage::kiFixedSize, NetworkMessages::ClientUnsubscribeMessage::kiFixedSize, 64};
		case PacketType::kClientResyncRequest:     return {NetworkMessages::ClientResyncRequestMessage::kiFixedSize, NetworkMessages::ClientResyncRequestMessage::kiFixedSize, 4};
		default:                                   return {}; // sentinel: not client-sendable
	}
}

// LAN discovery constants
inline constexpr uint16_t kuiDiscoveryPort = kuiDefaultPort + 1;
inline constexpr uint32_t kuiDiscoveryMagic = 0x42524B4E; // "BRKN"
inline constexpr std::chrono::milliseconds kDiscoveryScanDuration {1500};

// Network buffer size for ACK bitfield and snapshot ring buffers (decoupled from physics tick rate)
inline constexpr int64_t kiNetworkBufferSize = 128;

// Per-slot ACK tracking state (shared by client and server)
struct AckState
{
	int64_t iAckFloor = -1;
	uint64_t uiReceivedBitfieldLow = 0;   // bits 0-63
	uint64_t uiReceivedBitfieldHigh = 0;  // bits 64-127
	uint16_t uiEpoch = 0;
};

} // namespace engine
