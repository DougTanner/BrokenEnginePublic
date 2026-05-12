#pragma once

namespace engine
{

// One placed island instance inside a cell. Generator emits 1-4 per cell via
// rotated-AABB rejection sampling against per-template footprint.
struct IslandPlacement
{
	common::crc_t islandCrc = 0;
	XMFLOAT2 f2WorldPos {};
	float fRotation = 0.0f;
};

void GenerateIslandPlacements(GridCoord coord, std::vector<IslandPlacement>& rOut);

} // namespace engine
