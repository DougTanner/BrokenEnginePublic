#include "HexShields.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<HexShieldsPostRender>;

void HexShieldsInterpolate::Register()
{
}

void HexShieldsInterpolate::AllocateAndCopy(HexShieldsInterpolate& rCurrent, const HexShieldsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pfLightingIntensities, rPrevious.pfLightingIntensities, rCurrent.iCount * sizeof(rCurrent.pfLightingIntensities[0]));
		std::memcpy(rCurrent.pfSizes, rPrevious.pfSizes, rCurrent.iCount * sizeof(rCurrent.pfSizes[0]));
		std::memcpy(rCurrent.pfColorMixes, rPrevious.pfColorMixes, rCurrent.iCount * sizeof(rCurrent.pfColorMixes[0]));
	}
}

void HexShieldsPostRender::AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void HexShieldsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void HexShieldsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void HexShieldsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
