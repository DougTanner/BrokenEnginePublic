#pragma once

namespace game
{

struct FleetMember
{
	engine::global_id_t globalPlayerId {};
	bool bAlive = true;
	engine::GridCoord coord {};
};

struct Fleet
{
	std::vector<FleetMember> members;
	int64_t iFlagshipIndex = 0;
	engine::GridCoord wantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
	float fNavigationDelay = 2.0f;
	float fFrameChangeTimer = 0.0f;
};

} // namespace game
