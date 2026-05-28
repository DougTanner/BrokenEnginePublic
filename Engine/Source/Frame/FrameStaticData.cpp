#include "FrameStaticData.h"

namespace engine
{

void FrameStaticData::Write(std::ostream& rStream, bool bIncludeNavData) const
{
	common::Write(rStream, vecArea);
	common::Write(rStream, static_cast<int32_t>(islands.size()));
	for (const IslandPlacement& rPlacement : islands)
	{
		common::Write(rStream, rPlacement.islandCrc);
		common::Write(rStream, rPlacement.f2WorldPos);
		common::Write(rStream, rPlacement.fRotation);
	}
	if (bIncludeNavData)
	{
		navData.Write(rStream);
	}
}

void FrameStaticData::Read(std::istream& rStream, bool bIncludeNavData)
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
	if (bIncludeNavData)
	{
		navData.Read(rStream);
	}
	else
	{
		navData = {};
	}
	// Never serialized — purely local derived data. Clear so a network resend / save-load
	// forces RunFrameTick to rebuild from the freshly-read placements.
	elevationGrid = {};
}

} // namespace engine
