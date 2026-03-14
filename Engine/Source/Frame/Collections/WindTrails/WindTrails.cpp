#include "WindTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<WindTrailsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<WindTrailsPostRender>;

void WindTrailsInterpolate::Register()
{
}

void WindTrailsInterpolate::AllocateAndCopy(WindTrailsInterpolate& rCurrent, const WindTrailsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.pfIntensities, rPrevious.pfIntensities, rCurrent.iCount * sizeof(rCurrent.pfIntensities[0]));
		std::memcpy(rCurrent.pfWidths, rPrevious.pfWidths, rCurrent.iCount * sizeof(rCurrent.pfWidths[0]));
		std::memcpy(rCurrent.pfLengthMultipliers, rPrevious.pfLengthMultipliers, rCurrent.iCount * sizeof(rCurrent.pfLengthMultipliers[0]));
	}
}

void WindTrailsPostRender::AllocateAndCopy(WindTrailsPostRender& rCurrent, const WindTrailsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void WindTrailsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindTrailsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void WindTrailsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
