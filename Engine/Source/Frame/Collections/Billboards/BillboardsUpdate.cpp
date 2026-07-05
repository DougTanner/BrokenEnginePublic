#include "Billboards.h"

#if defined(BT_CLIENT)

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

	rBillboards.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rBillboards.puiTypeIndices[iIndex] = rData.uiTypeIndex;
	rBillboards.pFlags[iIndex] = rData.flags;
	rBillboards.pfRotations[iIndex] = rData.fRotation;
	rBillboards.pfExtra[iIndex] = rData.fExtra;
}

void BillboardsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void BillboardsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
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
	rInterpolate.pFlags[uiSpawnIndex] = {};
	rInterpolate.pfRotations[uiSpawnIndex] = 0.0f;
	rInterpolate.pfExtra[uiSpawnIndex] = 0.0f;
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
}

void BillboardsPostRender::Remove(game::Frame& __restrict rFrame, billboard_t& rId)
{
	ASSERT(rId.IsValid());

	BillboardsInterpolate& rInterpolate = rFrame.interpolate.billboards;
	BillboardsPostRender& rPostRender = rFrame.postRender.billboards;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void BillboardsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void BillboardsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine

#endif // BT_CLIENT
