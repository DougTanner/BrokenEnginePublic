#pragma once

namespace game
{

struct PlayersInterpolate;
using player_t = engine::id_t<PlayersInterpolate>;

} // namespace game

namespace engine
{

struct ClientCoordSubscription
{
	GridCoord coord {};
	bool bActive = false;
};

struct PendingSpawnRequest
{
	int64_t iClientId = 0;
	ClientRequestFlags_t flags {};
};

struct PendingDisconnect
{
	int64_t iClientId = 0;
	game::player_t playerId {};
	GridCoord coord {};
};

struct PendingNewSubscription
{
	int64_t iClientId = 0;
	int64_t iSlot = 0;
	GridCoord coord {};
};

struct GridUpdateData
{
	common::crc_t serverCrc = 0;
	common::crc_t inputCrc = 0;
	std::span<const game::StatusChange> statusChanges;
};

} // namespace engine
