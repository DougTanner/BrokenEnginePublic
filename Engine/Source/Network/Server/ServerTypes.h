#pragma once

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
	ClientGuid clientGuid {};
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
	std::span<const game::StatusChange> statusChanges;
};

} // namespace engine
