#include "Targets.h"

#include "Frame/FrameStaticData.h"

namespace engine
{
template struct Collection<game::TargetsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::TargetsPostRender>;
}

namespace game
{

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
	}
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

void TargetsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

void TargetsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame, [[maybe_unused]] const engine::FrameStaticData& rStaticData)
{
}

bool TargetsInterpolate::LogDifferences(const TargetsInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("TargetsInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
	}

	return bEqual;
}

bool TargetsPostRender::LogDifferences(const TargetsPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("TargetsPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::LogDifference<"puiIds">(i, puiIds[i], rOther.puiIds[i]);
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference<"puiSubscribers">(i, puiSubscribers[i], rOther.puiSubscribers[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

void TargetsInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace game
