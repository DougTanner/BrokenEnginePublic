#include "AreaLights.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<AreaLightsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<AreaLightsPostRender>;

void AreaLightsInterpolate::Register()
{
}

void AreaLightsInterpolate::AllocateAndCopy(AreaLightsInterpolate& rCurrent, const AreaLightsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		for (int64_t k = 0; k < 4; ++k)
		{
			std::memcpy(rCurrent.pVecVisiblePositions[k], rPrevious.pVecVisiblePositions[k], rCurrent.iCount * sizeof(rCurrent.pVecVisiblePositions[k][0]));
		}
		std::memcpy(rCurrent.pfIntensityMultipliers, rPrevious.pfIntensityMultipliers, rCurrent.iCount * sizeof(rCurrent.pfIntensityMultipliers[0]));
	}
}

void AreaLightsPostRender::AllocateAndCopy(AreaLightsPostRender& rCurrent, const AreaLightsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void AreaLightsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void AreaLightsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void AreaLightsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
