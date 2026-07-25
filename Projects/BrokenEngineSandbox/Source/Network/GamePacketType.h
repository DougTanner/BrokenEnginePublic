#pragma once

namespace game
{

enum class GamePacketType : uint8_t
{
	kServerAssignPlayer = static_cast<uint8_t>(engine::PacketType::kGamePacketStart),
	kServerPlayerState,
	kClientUpdatePlayerRequest,
	kClientCreateFleetRequest,
	kClientSpawnIntoFleetRequest,
	kClientRespawnInFleetRequest,
	kServerFleetSync,
	kClientDeleteFleetRequest,
	kClientFleetNavigationDelay,
	// Server-side debug control requests (one-way client -> server); handlers live in ServerSession::ParseReceivedGamePackets
	kClientSaveRequest,           // Client requests server quicksave (debug only)
	kClientLoadRequest,           // Client requests server quickload (debug only)
	kClientResetRequest,          // Client requests server reset (debug only)
	kClientReplayRecordRequest,   // Client requests server replay record start/stop (debug only)
	kClientReplayPlaybackRequest, // Client requests server replay playback (debug only)
	kClientPauseRequest,          // Client requests server pause/unpause (debug only)
	kClientTimespeedRequest,      // Client requests timescale change (debug only)
	kServerTimespeedUpdate,       // Server broadcasts current timescale to all clients
};

// Declarative per-packet contract for game-range client -> server packets, checked once at the ParseReceivedGamePackets
// dispatch choke point (ServerSession.cpp). Sizes are the FULL packet including the type byte; the dispatch site strips
// the type byte, so it compares each row against payload.size() + 1. A sentinel {} row (iMaxSize == 0) means the type is
// not client-sendable (server->client or unknown) -> contract violation.
constexpr engine::ClientPacketContract GetGamePacketContract(GamePacketType eType)
{
	switch (eType)
	{
		case GamePacketType::kClientUpdatePlayerRequest: return {14, 14, 8};   // 8B globalId + 1B bUseMissiles + 4B navDelay + 1B type
		case GamePacketType::kClientFleetNavigationDelay: return {21, 21, 8};  // 16B fleetGuid + 4B delay + 1B type (edge-triggered send)
		case GamePacketType::kClientCreateFleetRequest: return {1, 1, 4};      // 1B type only
		case GamePacketType::kClientDeleteFleetRequest: return {17, 17, 8};    // 16B fleetGuid + 1B type
		case GamePacketType::kClientSpawnIntoFleetRequest: return {17, 17, 8}; // 16B fleetGuid + 1B type
		case GamePacketType::kClientRespawnInFleetRequest: return {25, 25, 16}; // 16B fleetGuid + 8B memberIndex + 1B type

		// Debug-control packets: compile-time gated on kbDebugInput. When debug input is disabled the contract returns the
		// sentinel so a non-debug server treats them as not-client-sendable -> violation. Rationale: without the gate any
		// handshaken client could reset / pause / re-speed the whole server.
		case GamePacketType::kClientSaveRequest:
		case GamePacketType::kClientLoadRequest:
		case GamePacketType::kClientResetRequest:
		case GamePacketType::kClientReplayRecordRequest:
		case GamePacketType::kClientReplayPlaybackRequest:
			if constexpr (kbDebugInput) { return {1, 1, 2}; } else { return {}; } // 1B type only
		case GamePacketType::kClientPauseRequest:
			if constexpr (kbDebugInput) { return {2, 2, 4}; } else { return {}; } // 1B paused + 1B type
		case GamePacketType::kClientTimespeedRequest:
			if constexpr (kbDebugInput) { return {2, 2, 8}; } else { return {}; } // 1B direction + 1B type

		// Server -> client types and unknown -> not client-sendable.
		default: return {};
	}
}

} // namespace game
