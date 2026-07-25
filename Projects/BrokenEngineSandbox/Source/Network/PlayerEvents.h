#pragma once

namespace game
{

struct Fleet;

// Wire values for kServerPlayerState payload (must match server send order)
enum class PlayerStateWireType : uint8_t
{
	kSpawned,
	kChangedFrame,
	kDied,

	kCount, // Sentinel only — never serialized; bounds the player-state descriptor table (append wire values above it).
};

enum class PlayerEventType : uint8_t
{
	kAssigned,
	kSpawned,
	kChangedFrame,
	kDied,
};

struct ReceivedPlayerEvent
{
	PlayerEventType eType {};
	engine::global_id_t globalPlayerId {};
	engine::GridCoord coord {};
};

void ParsePlayerEvents(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, common::ScopedWorkbufferArena& rOutEventsArena);

bool ParseFleetSync(std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets, std::vector<Fleet>& rOutFleets);

} // namespace game
