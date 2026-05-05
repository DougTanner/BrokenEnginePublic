#include "FrameStaticData.h"

namespace engine
{

void FrameStaticData::Write(std::ostream& rStream) const
{
	common::Write(rStream, vecArea);
	common::Write(rStream, fIslandRotation);
	common::Write(rStream, f2IslandOffset);
	navData.Write(rStream);
}

void FrameStaticData::Read(std::istream& rStream)
{
	common::Read(rStream, vecArea);
	common::Read(rStream, fIslandRotation);
	common::Read(rStream, f2IslandOffset);
	navData.Read(rStream);
}

} // namespace engine
