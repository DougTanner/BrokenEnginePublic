#include "FrameStaticData.h"

#include "Frame/IslandTerrain.h"

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
	// Trust boundary (save / network full-state): enforce the generated per-cell contract and bound the count
	// against the stream before resize.
	common::ValidateDeserializedCountCapacity(iCount, kiMaxIslandsPerCell, sizeof(IslandPlacement::islandCrc) + sizeof(IslandPlacement::f2WorldPos) + sizeof(IslandPlacement::fRotation), rStream, "FrameStaticData::Read");
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
		// Wire-received navData is authoritative (client never rebuilds — server-only NavContour), so mark it
		// built regardless of vertex count.
		bNavDataBuilt = true;
	}
	else
	{
		navData = {};
		// Save-load only (the network-receive path takes the if-branch above with bIncludeNavData true): clear so
		// RunFrameTick (server) rebuilds navData via BuildCellNavData from the freshly-read placements.
		bNavDataBuilt = false;
	}
	// Never serialized — purely local derived data. Clear so a network resend / save-load
	// forces RunFrameTick to rebuild from the freshly-read placements.
	elevationGrid = {};
	islandRenderQueries = {};
}

void FrameStaticData::BuildRenderPlacementCache(const IslandTerrain& rIslandTerrain) const
{
	islandRenderQueries.resize(islands.size());
	for (size_t i = 0; i < islands.size(); ++i)
	{
		const IslandPlacement& rPlacement = islands[i];
		const IslandTemplate& rTemplate = rIslandTerrain.mIslands.at(rPlacement.islandCrc);
		IslandRenderQuery& rQuery = islandRenderQueries[i];

		// Negated rotation matches GlobalElevation's inverse-rotate world->local convention. Deterministic
		// sin/cos (as BlendPlacementIntoGrid hoists) — render-only, so libm-vs-polynomial ulp drift is fine.
		common::SinCos sinCos = common::DeterministicSinCos(-rPlacement.fRotation);
		rQuery.fCos = sinCos.fCos;
		rQuery.fSin = sinCos.fSin;
		rQuery.f2WorldPos = rPlacement.f2WorldPos;
		rQuery.fFootprintX = rTemplate.mfQuadFootprintX;
		rQuery.fFootprintY = rTemplate.mfQuadFootprintY;
		rQuery.pHeightmapHalf = rTemplate.mpHeightmapHalf;
		rQuery.iHeightmapWidth = rTemplate.miHeightmapWidth;
		rQuery.iHeightmapHeight = rTemplate.miHeightmapHeight;
	}
}

} // namespace engine
