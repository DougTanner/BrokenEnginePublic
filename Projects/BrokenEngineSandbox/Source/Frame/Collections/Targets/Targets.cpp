#include "Targets.h"

#include "Frame/FrameStaticData.h"

namespace engine
{
template struct Collection<game::TargetsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::TargetsPostRender>;
}

namespace game
{

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

void TargetsPostRender::AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

bool TargetsInterpolate::LogDifferences(const TargetsInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("TargetsInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
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

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
	{
		bEqual &= common::LogDifference<"puiIds">(i, puiIds[i], rOther.puiIds[i]);
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
		bEqual &= common::LogDifference<"puiSubscribers">(i, puiSubscribers[i], rOther.puiSubscribers[i]);
		bEqual &= common::LogDifference<"pAlignments">(i, pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

} // namespace game
