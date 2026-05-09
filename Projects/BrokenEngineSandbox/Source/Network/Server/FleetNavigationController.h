#pragma once

#if defined(BT_SERVER)

#include "Fleet.h"

namespace game
{

struct PendingFlagshipUpdate
{
	engine::ClientGuid clientGuid {};
	int64_t iFleetIndex = 0;
	engine::GridCoord newWantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
};

class FleetNavigationController
{
public:

	void TickFleetTimers(std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets, common::RandomEngine& rRandom);
	void ProcessFlagshipUpdates(const std::unordered_map<engine::ClientGuid, std::vector<Fleet>, engine::ClientGuidHash>& rFleets);

	void QueueFlagshipUpdate(const PendingFlagshipUpdate& rUpdate);
	void ClearPendingFlagshipUpdates();

	void ShiftFlagshipAfterDeath(const engine::ClientGuid& rGuid, int64_t iFleetIndex, Fleet& rFleet);

private:

	std::vector<PendingFlagshipUpdate> mPendingFlagshipUpdates;
};

} // namespace game

#endif // BT_SERVER
