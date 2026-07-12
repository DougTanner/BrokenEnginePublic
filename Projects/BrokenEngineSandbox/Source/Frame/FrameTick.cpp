#include "FrameTick.h"

#include "Frame/FrameStaticData.h"
#include "Frame/IslandTerrain.h"
#include "Frame/NavBuild.h"

#include "Frame/FrameCollections.h"

namespace game
{

void RunFrameTick(const ActiveFrameRef& rRef, int64_t iTickCounter, float fCurrentTime)
{
	// Mark this thread as inside a deterministic tick so a stray render-path GlobalElevation/GlobalNormal
	// call (which walks mCoordFrames with libm trig) fails fast instead of silently desyncing across CPUs.
	common::FrameTickScope frameTickScope;

	// Verify MXCSR has not been corrupted by external calls (audio, Vulkan, etc.)
	unsigned int uiControlWord = 0;
	_controlfp_s(&uiControlWord, 0, 0);
	ASSERT((uiControlWord & _MCW_DN) == _DN_FLUSH);
	ASSERT((uiControlWord & _MCW_RC) == _RC_NEAR);

	Frame& rNext = *rRef.pNext;
	const Frame& rCurrent = *rRef.pCurrent;
	const engine::FrameStaticData& rStaticData = *rRef.pStaticData;

#if defined(BT_SERVER)
	// NavData is derived from placements + per-template NavContour. Build it here on the
	// per-coord dispatch thread (naturally parallel across coords) on the first tick after
	// a coord is created or reloaded from save. Client receives prebuilt navData over the
	// wire and never enters this branch (server-only NavContour).
	if (!rStaticData.bNavDataBuilt && !rStaticData.islands.empty())
	{
		ScopedSuppressAllocationTracking suppress;
		engine::BuildCellNavData(rStaticData.navData, rStaticData.islands);
		rStaticData.bNavDataBuilt = true;
	}
#endif

	// Per-cell elevation grid (purely derived from islands + shared heightmaps). Both client
	// and server build their own bit-identical copy here — same deterministic placements, same
	// shared heightmaps, /fp:strict math — so it stays out of the CRC and is never serialized.
	// Builds before any sim phase below so every FrameElevation/FrameNormal caller this tick
	// sees a populated grid.
	if (rStaticData.elevationGrid.empty() && !rStaticData.islands.empty())
	{
		ScopedSuppressAllocationTracking suppress;
		engine::gpIslandTerrain->BuildElevationGrid(rStaticData.coord, rStaticData.islands, rStaticData.elevationGrid);
	}

#if defined(BT_CLIENT)
	// Render-only cache: per-placement flattened query (inverse-rotation sin/cos + template footprint /
	// heightmap pointer / dims) for GlobalElevation/GlobalNormal (ProjectToBaseHeight). Built here alongside
	// the elevation grid — placement rotation and template are static for the cell's life — so the render
	// path does zero hash lookups and zero libm trig per call. The server never calls GlobalElevation, so
	// skip it there.
	if (rStaticData.islandRenderQueries.empty() && !rStaticData.islands.empty())
	{
		ScopedSuppressAllocationTracking suppress;
		rStaticData.BuildRenderPlacementCache(*engine::gpIslandTerrain);
	}
#endif

	// Phase 1: Interpolate
	FrameInterpolate::AllocateAndCopy(rNext.interpolate, rCurrent.interpolate);
	FrameInterpolate::Update(rNext.interpolate, rCurrent, kfDeltaTime);
	rNext.interpolate.iTick = iTickCounter;
	rNext.interpolate.fCurrentTime = fCurrentTime;

	// Phase 2: PostRender
	FramePostRender::AllocateAndCopy(rNext.postRender, rCurrent.postRender);
	FramePostRender::Update(rNext, rCurrent, *rRef.pFrameInput, rStaticData);

	// Phase 3: Collision
	FramePostRender::PreCollision(rNext, rCurrent, rStaticData);
	engine::Collision::Collide(rNext.postRender.alignments, rStaticData.vecArea);
	FramePostRender::PostCollision(rNext, rCurrent, rStaticData);
	FramePostRender::AreaDamage(rNext, rCurrent, rStaticData);

	// Phase 4: Transfer
	FramePostRender::Transfer(rNext, rStaticData);

	// Phase 5: Destroy/Spawn
	FramePostRender::Destroy(rNext, rStaticData);
	FramePostRender::Spawn(rNext, *rRef.pFrameInput, rStaticData);

	// Compute CRCs after all phases complete
	rNext.postRender.sharedCrc = rNext.Crcs();
}

} // namespace game
