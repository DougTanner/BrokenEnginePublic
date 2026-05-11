#pragma once

#include "Frame/IslandPlacement.h"
#include "Frame/NavBuild.h"

namespace engine
{

struct FrameStaticData
{
	XMVECTOR vecArea {};
	GridCoord coord {};
	std::vector<IslandPlacement> islands;
	NavData navData;

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

} // namespace engine
