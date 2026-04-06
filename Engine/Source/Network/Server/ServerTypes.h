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
	std::vector<global_player_t> playerIds;
	std::vector<GridCoord> coords;
};

struct PendingNewSubscription
{
	int64_t iClientId = 0;
	int64_t iSlot = 0;
	GridCoord coord {};
};

struct GridUpdateData
{
	common::crc_t sharedCrc = 0;
	common::crc_t inputCrc = 0;
	std::span<const game::StatusChange> statusChanges;
};

struct PendingUpdatePlayerRequest
{
	int64_t iClientId = 0;
	global_player_t globalPlayerId {};
	bool bUseMissiles = false;
	float fNavigationDelay = 2.0f;
};

} // namespace engine
