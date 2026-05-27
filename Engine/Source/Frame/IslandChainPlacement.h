#pragma once

namespace engine
{

// One placed island instance inside a cell. Emitted by IslandChainPlacement::Generate; index 0 is
// always the dominant anchor island (consumers index islands.at(0) / islands.at(i % size)).
struct IslandPlacement
{
	common::crc_t islandCrc = 0;
	XMFLOAT2 f2WorldPos {};
	float fRotation = 0.0f;
};

// Deterministic per-cell archipelago generator. Composes a believable island chain — a large anchor
// in the SW third of the cell, a golden log-spiral sweep of medium islands toward the NE, low islands
// on the outside of the curve, and small low islets scattered around the anchor — packing every island
// by its rotated true valid-area hull so no two hulls overlap (bounding rectangles may overlap, hidden
// underwater). Seeded only by the grid coord, so client and server produce identical layouts.
class IslandChainPlacement
{
public:

	static void Generate(GridCoord coord, std::vector<IslandPlacement>& rOut);
};

} // namespace engine
