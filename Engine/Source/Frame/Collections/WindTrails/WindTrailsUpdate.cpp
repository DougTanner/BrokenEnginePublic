#include "WindTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

void WindTrailsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	WindTrailsInterpolate& rWindTrails = rFrameInterpolate.windTrails;
	int64_t iIndex = rWindTrails.IdToIndex(id);

	rWindTrails.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rWindTrails.pfIntensities[iIndex] = rData.fIntensity;
	rWindTrails.pfWidths[iIndex] = rData.fWidth;
	rWindTrails.pfLengthMultipliers[iIndex] = rData.fLengthMultiplier;
}

void WindTrailsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::Add(game::Frame& __restrict rFrame, wind_trail_t& rId)
{
	ASSERT(!rId.IsValid());

	WindTrailsInterpolate& rInterpolate = rFrame.interpolate.windTrails;
	WindTrailsPostRender& rPostRender = rFrame.postRender.windTrails;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void WindTrailsPostRender::Remove(game::Frame& __restrict rFrame, wind_trail_t& rId)
{
	ASSERT(rId.IsValid());

	WindTrailsInterpolate& rInterpolate = rFrame.interpolate.windTrails;
	WindTrailsPostRender& rPostRender = rFrame.postRender.windTrails;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void WindTrailsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void WindTrailsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
