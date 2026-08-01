#pragma once

#if defined(BT_SERVER)

#include "Frame/GridCoord.h"

namespace game
{

struct StatusChange;

} // namespace game

namespace engine
{

enum class SubscriptionFlags : uint8_t
{
	kActive            = 1 << 0,
	kFirstUpdateLogged = 1 << 1,
};

struct ClientCoordSubscription
{
	GridCoord coord {};
	common::Flags<SubscriptionFlags> flags;
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

#endif // BT_SERVER
