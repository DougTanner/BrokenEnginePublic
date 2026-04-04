#include "Targets.h"

#include "Frame/FrameStaticData.h"

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TargetsInterpolate& rTargets = *rFrameInterpolate.pTargets;
	int64_t iIndex = rTargets.IdToIndex(id);

	rTargets.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rTargets.puiTypeIndices[iIndex] = rData.uiTypeIndex;
}

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Owner (Spaceships) writes position and type index via IdToIndex pattern.
}

void TargetsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::Add(Frame& __restrict rFrame, target_t& rId, uint8_t uiTargetTypeIndex, engine::alignment_t alignment)
{
	TargetsInterpolate& rInterpolate = *rFrame.interpolate.pTargets;
	TargetsPostRender& rPostRender = *rFrame.postRender.pTargets;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorSetW(XMVectorZero(), 1.0f);
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTargetTypeIndex;
	rPostRender.pFlags[uiSpawnIndex] = {};
	rPostRender.puiSubscribers[uiSpawnIndex] = 0;
	rPostRender.pAlignments[uiSpawnIndex] = alignment;
}

void TargetsPostRender::Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags)
{
	TargetsInterpolate& rInterpolate = *rFrame.interpolate.pTargets;
	TargetsPostRender& rPostRender = *rFrame.postRender.pTargets;

	int64_t iIndex = rInterpolate.IdToIndex(rId);

	if (flags & kDestination)
	{
		// Spaceship died - kill target immediately regardless of subscribers
		engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
		rId = {};
		return;
	}

	// Subscriber removal path
	ASSERT(rPostRender.puiSubscribers[iIndex] > 0);
	--rPostRender.puiSubscribers[iIndex];

	// Only actually remove when both conditions are met:
	// - No destination flag set (owner removed their reference)
	// - No subscribers remaining (no missiles tracking this target)
	if (!(rPostRender.pFlags[iIndex] & kDestination) && rPostRender.puiSubscribers[iIndex] == 0)
	{
		engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
		rId = {};
	}
}

void TargetsPostRender::AddSubscriber(Frame& __restrict rFrame, target_t id)
{
	TargetsPostRender& rPostRender = *rFrame.postRender.pTargets;
	int64_t iIndex = rFrame.interpolate.pTargets->IdToIndex(id);
	++rPostRender.puiSubscribers[iIndex];
}

} // namespace game
