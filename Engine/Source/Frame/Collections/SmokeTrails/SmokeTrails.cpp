#include "SmokeTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<SmokeTrailsPostRender>;

void SmokeTrailsInterpolate::Register()
{
}

void SmokeTrailsInterpolate::AllocateAndCopy(SmokeTrailsInterpolate& rCurrent, const SmokeTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.pfIntensities, rPrevious.pfIntensities, rCurrent.iCount * sizeof(rCurrent.pfIntensities[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
	}
}

void SmokeTrailsPostRender::AllocateAndCopy(SmokeTrailsPostRender& rCurrent, const SmokeTrailsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void SmokeTrailsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void SmokeTrailsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void SmokeTrailsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
