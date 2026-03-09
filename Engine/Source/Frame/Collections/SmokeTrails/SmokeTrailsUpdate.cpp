#include "SmokeTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

void SmokeTrailsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	// Smoothing is handled in Render() using static render state
}

void SmokeTrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	SmokeTrailsInterpolate& rSmokeTrails = rFrameInterpolate.smokeTrails;
	int64_t iIndex = rSmokeTrails.IdToIndex(id);

	rSmokeTrails.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rSmokeTrails.pfIntensities[iIndex] = rData.fIntensity;
}

void SmokeTrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::Add(game::Frame& __restrict rFrame, smoke_trails_t& rId, uint8_t uiTypeIndex, smoke_trails_t reuseId)
{
	ASSERT(!rId.IsValid());

	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());

	smoke_trails_t id;
	int64_t iSpawnIndex = 0;
	if (reuseId.IsValid())
	{
		auto [index, reusedId] = AddIndexableElementWithId(rInterpolate, rPostRender, reuseId);
		iSpawnIndex = index;
		id = reusedId;
	}
	else
	{
		auto [index, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
		iSpawnIndex = index;
		id = newId;
	}

	rId = id;
	rPostRender.puiIds[iSpawnIndex] = id;
	rInterpolate.puiTypeIndices[iSpawnIndex] = uiTypeIndex;
	if (reuseId.IsValid())
	{
		rInterpolate.pfStartTimes[iSpawnIndex] = 0.0f;
	}
	else
	{
		rInterpolate.pfStartTimes[iSpawnIndex] = rFrame.interpolate.fCurrentTime;
	}
}

void SmokeTrailsPostRender::Remove(game::Frame& __restrict rFrame, smoke_trails_t& rId)
{
	ASSERT(rId.IsValid());

	SmokeTrailsInterpolate& rInterpolate = rFrame.interpolate.smokeTrails;
	SmokeTrailsPostRender& rPostRender = rFrame.postRender.smokeTrails;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void SmokeTrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SmokeTrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
