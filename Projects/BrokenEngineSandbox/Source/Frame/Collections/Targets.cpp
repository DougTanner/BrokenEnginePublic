#include "Targets.h"

#include "Frame/Frame.h"

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void TargetsInterpolate::Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TargetsInterpolate& rTargets = rFrameInterpolate.targets;
	int64_t iIndex = rTargets.IdToIndex(id);

	rTargets.pVecPositions[iIndex] = rData.vecPosition;
	rTargets.puiTypeIndices[iIndex] = rData.uiTypeIndex;
}

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	TargetsInterpolate& rCurrent = rCurrentFrameInterpolate.targets;

	// Owner (Spaceships) writes position and type index via IdToIndex pattern.

	PROFILE_SET_COUNT(engine::kCpuCounterTargets, rCurrent.iCount);
}

void TargetsPostRender::AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, static_cast<size_t>(rCurrent.iCount) * sizeof(target_t));
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, static_cast<size_t>(rCurrent.iCount) * sizeof(TargetFlags_t));
		std::memcpy(rCurrent.puiSubscribers, rPrevious.puiSubscribers, static_cast<size_t>(rCurrent.iCount) * sizeof(uint8_t));
	}
}

void TargetsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void TargetsInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

void TargetsPostRender::PreCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void TargetsPostRender::PostCollision([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void TargetsPostRender::AreaDamage([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
}

void TargetsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Add(Frame& __restrict rFrame, target_t& rId, uint8_t uiTargetTypeIndex)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTargetTypeIndex;
	rPostRender.pFlags[uiSpawnIndex] = {};
	rPostRender.puiSubscribers[uiSpawnIndex] = 0;
}

void TargetsPostRender::Remove(Frame& __restrict rFrame, target_t& rId, TargetFlags_t flags)
{
	TargetsInterpolate& rInterpolate = rFrame.interpolate.targets;
	TargetsPostRender& rPostRender = rFrame.postRender.targets;

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
	TargetsPostRender& rPostRender = rFrame.postRender.targets;
	int64_t iIndex = rFrame.interpolate.targets.IdToIndex(id);
	++rPostRender.puiSubscribers[iIndex];
}

void TargetsPostRender::RegisterType(uint8_t& ruiIndex, const TargetsType& rType)
{
	ASSERT(ruiIndex == 0xFF);
	ruiIndex = static_cast<uint8_t>(TargetsInterpolate::sTypes.size());
	TargetsInterpolate::sTypes.push_back(rType);
}

bool TargetsInterpolate::operator==(const TargetsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
	}

	return bEqual;
}

bool TargetsPostRender::operator==(const TargetsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(puiSubscribers[i], rOther.puiSubscribers[i]);
	}

	return bEqual;
}

} // namespace game
