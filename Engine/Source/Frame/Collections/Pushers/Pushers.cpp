#include "Pushers.h"

#if defined(BT_CLIENT)
#include "Profile/ProfileManager.h"
#endif

namespace engine
{

template struct Collection<PushersInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<PushersPostRender>;

void PushersInterpolate::AllocateAndCopy(PushersInterpolate& rCurrent, const PushersInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void PushersPostRender::AllocateAndCopy(PushersPostRender& rCurrent, const PushersPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

bool PushersInterpolate::LogDifferences(const PushersInterpolate& rOther) const
{
	common::ScopedLogDifferenceContext context("PushersInterpolate");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
	{
		bEqual &= common::LogDifference_Vec("pVecPositions", i, pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::LogDifference<"pfRadii">(i, pfRadii[i], rOther.pfRadii[i]);
		bEqual &= common::LogDifference<"pfIntensities">(i, pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::LogDifference<"pfPowers">(i, pfPowers[i], rOther.pfPowers[i]);
		bEqual &= common::LogDifference<"pFlags">(i, pFlags[i], rOther.pFlags[i]);
	}

	return bEqual;
}

bool PushersPostRender::LogDifferences(const PushersPostRender& rOther) const
{
	common::ScopedLogDifferenceContext context("PushersPostRender");
	bool bEqual = true;
	bEqual &= Collection::LogDifferences(rOther);

	for (int64_t i = 0; i < CommonRowCount(rOther); ++i)
	{
		bEqual &= common::LogDifference<"puiIds">(i, puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

#if defined(BT_CLIENT)

static int64_t siTotalCount = 0;

void PushersInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	siTotalCount = 0;
}

void PushersInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	siTotalCount += rFrameInterpolate.pushers.iCount;
}

void PushersInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterPushers, siTotalCount);
}

#endif // BT_CLIENT

} // namespace engine
