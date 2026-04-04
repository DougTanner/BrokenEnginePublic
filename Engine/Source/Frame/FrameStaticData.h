#pragma once

#include "Graphics/IslandsFlip.h"

namespace engine
{

struct FrameStaticData
{
	XMVECTOR vecArea {};
	IslandsFlip eIslandsFlip = kFlipNone;
	XMFLOAT2 f2IslandOffset {};

	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);
};

} // namespace engine
