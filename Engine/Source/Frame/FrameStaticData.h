#pragma once

#include "Frame/GridCoord.h"
#include "Frame/IslandChainPlacement.h"
#include "Frame/NavBuild.h"

namespace engine
{

class IslandTerrain;

// Flattened per-placement render query, index-parallel to FrameStaticData::islands. Holds everything
// GlobalElevation's loop body reads, copied contiguous at cache-build time from the IslandPlacement plus
// its IslandTemplate (resolved via one mIslands lookup during the build), so the render path does zero
// hash lookups and zero libm trig per island. Render-only (client builds it; server never calls
// GlobalElevation). Never serialized.
struct IslandRenderQuery
{
	float fCos = 1.0f;
	float fSin = 0.0f;
	XMFLOAT2 f2WorldPos {};
	float fFootprintX = 0.0f;
	float fFootprintY = 0.0f;
	const uint16_t* pHeightmapHalf = nullptr;
	int32_t iHeightmapWidth = 0;
	int32_t iHeightmapHeight = 0;
};

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

	// Whether navData reflects a completed build/deserialize for this cell's current placements. Gates the
	// lazy build so a cell that legitimately yields zero nav vertices (all contours degenerate/underwater)
	// is not rebuilt every tick — an empty navData.vertices is a valid built result, not a "not yet built"
	// sentinel. Set true after BuildCellNavData (server) or Read(true) (client wire); reset false only on
	// Read(false) (save-load), which clears navData so the reloaded cell rebuilds. Like navData, lives outside
	// the CRC and the persisted save format.
	mutable bool bNavDataBuilt = false;

	// Derived from islands + per-template heightmaps. Built lazily on the per-coord
	// frame-tick thread (both client and server build their own bit-identical copy from
	// the same deterministic placements), refreshed when the network resends staticData,
	// so it lives outside the persisted save format. mutable lets RunFrameTick fill it
	// through the const FrameStaticData& it gets from ActiveFrameRef. Sized
	// game::Frame::kiElevationGridDim^2 when populated.
	mutable std::vector<float> elevationGrid;

	// Render-only per-placement query cache, index-parallel to islands. Consumed by the client's
	// GlobalElevation/GlobalNormal (ProjectToBaseHeight): each entry flattens the placement's rotation
	// sin/cos plus its template's footprint / heightmap pointer / dims, so RunFrameTick caches it once
	// (alongside elevationGrid) and the render path does zero hash lookups and zero libm trig per call.
	// Built client-side only (the server never calls GlobalElevation); rebuilt after any placement edit
	// via the same empty-check + clear-on-mutate protocol as elevationGrid. Never serialized (derived
	// from the placements + shared templates).
	mutable std::vector<IslandRenderQuery> islandRenderQueries;

	void Write(std::ostream& rStream, bool bIncludeNavData) const;
	void Read(std::istream& rStream, bool bIncludeNavData);

	// Fill islandRenderQueries from islands: DeterministicSinCos(-fRotation) plus the footprint / heightmap
	// fields copied from each placement's template (one rIslandTerrain.mIslands lookup per placement).
	// Called by RunFrameTick on the client when the cache is empty; see the member comment above.
	void BuildRenderPlacementCache(const IslandTerrain& rIslandTerrain) const;
};

} // namespace engine
