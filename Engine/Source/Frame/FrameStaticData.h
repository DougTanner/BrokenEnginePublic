#pragma once

#include "Frame/NavBuild.h"

namespace engine
{

struct FrameStaticData
{
	XMVECTOR vecArea {};
	GridCoord coord {};
	float fIslandRotation = 0.0f;
	XMFLOAT2 f2IslandOffset {};
	NavData navData;

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

} // namespace engine
