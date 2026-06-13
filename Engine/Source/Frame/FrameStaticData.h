#pragma once

#include "Frame/GridCoord.h"
#include "Frame/IslandChainPlacement.h"
#include "Frame/NavBuild.h"

namespace engine
{

struct FrameStaticData
{
	XMVECTOR vecArea {};
	GridCoord coord {};
	std::vector<IslandPlacement> islands;
	// Derived from islands + per-template NavContour. Built lazily on the per-coord
	// frame-tick thread (server-only) and refreshed when the network resends staticData,
	// so it lives outside the persisted save format. mutable lets RunFrameTick fill it
	// through the const FrameStaticData& it gets from ActiveFrameRef.
	mutable NavData navData;

	// Derived from islands + per-template heightmaps. Built lazily on the per-coord
	// frame-tick thread (both client and server build their own bit-identical copy from
	// the same deterministic placements), refreshed when the network resends staticData,
	// so it lives outside the persisted save format. mutable lets RunFrameTick fill it
	// through the const FrameStaticData& it gets from ActiveFrameRef. Sized
	// game::Frame::kiElevationGridDim^2 when populated.
	mutable std::vector<float> elevationGrid;

	void Write(std::ostream& rStream, bool bIncludeNavData) const;
	void Read(std::istream& rStream, bool bIncludeNavData);
};

} // namespace engine
