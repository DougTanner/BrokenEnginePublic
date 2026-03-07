#include "Billboards.h"

#ifdef BT_CLIENT

namespace engine
{

using enum BillboardFlags;

void BillboardsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	BillboardsInterpolate& rBillboards = rFrameInterpolate.billboards;
	int64_t iIndex = rBillboards.IdToIndex(id);

	rBillboards.pVecPositions[iIndex] = rData.vecPosition;
	rBillboards.puiTypeIndices[iIndex] = rData.uiTypeIndex;
	rBillboards.puiFlags[iIndex] = rData.uiFlags;
	rBillboards.pfRotations[iIndex] = rData.fRotation;
	rBillboards.pfExtra[iIndex] = rData.fExtra;
}

void BillboardsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	BillboardsPostRender& __restrict rCurrent = rFrame.postRender.billboards;
	const BillboardsPostRender& __restrict rPrevious = rPreviousFrame.postRender.billboards;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		billboard_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void BillboardsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsPostRender::Add(game::Frame& __restrict rFrame, billboard_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
}

void BillboardsPostRender::Remove(game::Frame& __restrict rFrame, billboard_t& rId)
{
	ASSERT(rId.IsValid());

	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void BillboardsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void BillboardsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
