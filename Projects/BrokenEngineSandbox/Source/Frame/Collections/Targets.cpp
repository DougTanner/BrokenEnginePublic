#include "Targets.h"

#include "Profile/ProfileManager.h"

namespace engine
{
template struct Collection<game::TargetsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::TargetsPostRender>;
}

namespace game
{

using enum TargetFlags;

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
	}
}

void TargetsInterpolate::Sync(FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	TargetsInterpolate& rTargets = *rFrameInterpolate.pTargets;
	int64_t iIndex = rTargets.IdToIndex(id);

	rTargets.pVecPositions[iIndex] = rData.vecPosition;
	rTargets.puiTypeIndices[iIndex] = rData.uiTypeIndex;
}

void TargetsInterpolate::Update([[maybe_unused]] FrameInterpolate& __restrict rCurrentFrameInterpolate, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
{
	// Owner (Spaceships) writes position and type index via IdToIndex pattern.
	[[maybe_unused]] TargetsInterpolate& rCurrent = *rCurrentFrameInterpolate.pTargets;
	gpProfileManager->SetCount(game::kCpuCounterTargets, rCurrent.iCount);
}

void TargetsPostRender::AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
		std::memcpy(rCurrent.puiSubscribers, rPrevious.puiSubscribers, rCurrent.iCount * sizeof(rCurrent.puiSubscribers[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void TargetsPostRender::Update([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const Frame& __restrict rPreviousFrame)
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

void TargetsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
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

	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
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
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

bool TargetsInterpolate::ServerCompare(const TargetsInterpolate& rOther) const { return *this == rOther; }

bool TargetsPostRender::ServerCompare(const TargetsPostRender& rOther) const { return *this == rOther; }

void TargetsInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace game
