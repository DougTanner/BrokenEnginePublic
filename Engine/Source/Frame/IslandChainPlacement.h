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

// Chain links after the Huge anchor (a fixed sequence, truncated by the cell edge).
inline constexpr int64_t kiChainLargeCount = 2;
inline constexpr int64_t kiChainMediumCount = 3;

// Maximum number of small surround slots per big island.
inline constexpr int64_t kiMaxSurroundSlots = 16;

// A few extra smalls trailing off the end of the curve (attached to the chain tip, biased forward).
inline constexpr int64_t kiTailSmallCount = 5;

// Worst-case placements per cell (every big island + a full surround ring on each + the tail). The
// placement generator reserves this capacity so its ConvexHull2D views remain pointer-stable.
inline constexpr int64_t kiMaxBigIslands = 1 + kiChainLargeCount + kiChainMediumCount;
inline constexpr int64_t kiMaxIslandsPerCell = kiMaxBigIslands + kiMaxBigIslands * kiMaxSurroundSlots + kiTailSmallCount;

// Deterministic per-cell archipelago generator. Builds a chain by contact growth — a Huge anchor
// in the SW third of the cell, then a fixed sequence of Large/Medium links each placed just-touching
// the chain tip along a hard-turning curve (truncated where the next link would leave the cell), then
// Small islets ringing each big island — packing every island by its rotated true valid-area hull so
// no two hulls overlap (bounding rectangles may overlap, hidden underwater). Seeded only by the grid
// coord, so client and server produce identical layouts.
void GenerateIslandChain(GridCoord coord, std::vector<IslandPlacement>& rOut);

} // namespace engine
