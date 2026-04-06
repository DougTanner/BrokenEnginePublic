#pragma once

#include "Frame/NavBuild.h"
#include "Graphics/IslandsFlip.h"

namespace engine
{

struct FrameStaticData
{
	XMVECTOR vecArea {};
	GridCoord coord {};
	IslandsFlip eIslandsFlip = kFlipNone;
	XMFLOAT2 f2IslandOffset {};
	NavData navData;

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

} // namespace engine
