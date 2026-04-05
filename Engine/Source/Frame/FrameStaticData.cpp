#include "FrameStaticData.h"

namespace engine
{

void FrameStaticData::Write(std::ostream& rStream) const
{
	common::Write(rStream, vecArea);
	common::Write(rStream, eIslandsFlip);
	common::Write(rStream, f2IslandOffset);
	navData.Write(rStream);
}

void FrameStaticData::Read(std::istream& rStream)
{
	common::Read(rStream, vecArea);
	common::Read(rStream, eIslandsFlip);
	common::Read(rStream, f2IslandOffset);
	navData.Read(rStream);
}

} // namespace engine
