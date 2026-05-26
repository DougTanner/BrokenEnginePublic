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
};

} // namespace game
