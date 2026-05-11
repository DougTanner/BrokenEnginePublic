#pragma once

namespace engine
{

// One placed island instance inside a cell. Phase 2 emits exactly one per cell;
// later phases extend the list to N per cell.
struct IslandPlacement
{
	common::crc_t islandCrc = 0;
	XMFLOAT2 f2WorldPos {};
	float fRotation = 0.0f;
};

void GenerateIslandPlacements(GridCoord coord, std::vector<IslandPlacement>& rOut);

} // namespace engine
