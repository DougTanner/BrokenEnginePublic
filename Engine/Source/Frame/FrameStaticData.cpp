#include "FrameStaticData.h"

namespace engine
{

void FrameStaticData::Write(std::ostream& rStream) const
{
	common::Write(rStream, vecArea);
	common::Write(rStream, static_cast<int32_t>(islands.size()));
	for (const IslandPlacement& rPlacement : islands)
	{
		common::Write(rStream, rPlacement.islandCrc);
		common::Write(rStream, rPlacement.f2WorldPos);
		common::Write(rStream, rPlacement.fRotation);
	}
	navData.Write(rStream);
}

void FrameStaticData::Read(std::istream& rStream)
{
	common::Read(rStream, vecArea);
	int32_t iCount = 0;
	common::Read(rStream, iCount);
	islands.resize(iCount);
	for (int32_t i = 0; i < iCount; ++i)
	{
		common::Read(rStream, islands.at(i).islandCrc);
		common::Read(rStream, islands.at(i).f2WorldPos);
		common::Read(rStream, islands.at(i).fRotation);
	}
	navData.Read(rStream);
}

} // namespace engine
