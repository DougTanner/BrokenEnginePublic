#pragma once

namespace game
{

// Server-minted random 128-bit fleet identifier; survives disconnect/reconnect, save/load, and full client restart.
// Mirrors engine::ClientGuid shape.
struct FleetGuid
{
	uint64_t uiHigh = 0;
	uint64_t uiLow = 0;

	bool IsValid() const { return uiHigh != 0 || uiLow != 0; }
	bool operator==(const FleetGuid&) const = default;
};

struct FleetGuidHash
{
	size_t operator()(const FleetGuid& rGuid) const
	{
		return std::hash<uint64_t>{}(rGuid.uiHigh) ^ (std::hash<uint64_t>{}(rGuid.uiLow) << 1);
	}
};

struct FleetMember
{
	engine::global_id_t globalPlayerId {};
	bool bAlive = true;
	engine::GridCoord coord {};
};

struct Fleet
{
	FleetGuid guid {};
	std::vector<FleetMember> members;
	int64_t iFlagshipIndex = 0;
	engine::GridCoord wantedCoord {};
	uint8_t uiPendingFleetWantedCoordTicks = 0;
	float fNavigationDelay = 60.0f;
	float fFrameChangeTimer = 0.0f;
};

} // namespace game
