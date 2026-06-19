#pragma once

#include "Frame/GridCoord.h"

namespace engine
{

// One placed island instance inside a cell. Emitted by GenerateIslandChain; index 0 is
// always the dominant anchor island (consumers index islands.at(0) / islands.at(i % size)).
struct IslandPlacement
{
	common::crc_t islandCrc = 0;
	XMFLOAT2 f2WorldPos {};
	float fRotation = 0.0f;
};

// Deterministic per-cell archipelago generator. Builds a chain by contact growth — a Huge anchor
// in the SW third of the cell, then a fixed sequence of Large/Medium links each placed just-touching
// the chain tip along a hard-turning curve (truncated where the next link would leave the cell), then
// Small islets ringing each big island — packing every island by its rotated true valid-area hull so
// no two hulls overlap (bounding rectangles may overlap, hidden underwater). Seeded only by the grid
// coord, so client and server produce identical layouts.
void GenerateIslandChain(GridCoord coord, std::vector<IslandPlacement>& rOut);

} // namespace engine
