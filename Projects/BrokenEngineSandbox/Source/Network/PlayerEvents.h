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

void ParsePlayerEvents(
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets,
	std::vector<ReceivedPlayerEvent>& rOutEvents);

void ParseFleetSync(
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& rRawPackets,
	std::vector<Fleet>& rOutFleets);

inline uint8_t PlayerEventTypeToWire(PlayerEventType eType)
{
	// kAssigned has no wire equivalent (uses kServerAssignPlayer packet type)
	// kSpawned/kChangedFrame/kDied map to wire values 0/1/2
	return static_cast<uint8_t>(eType) - 1;
}

} // namespace game
