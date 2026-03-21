#include "WindRadials.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<WindRadialsInterpolate>;
template struct Collection<WindRadialsPostRender>;

void WindRadialsInterpolate::Register()
{
}

void WindRadialsInterpolate::AllocateAndCopy(WindRadialsInterpolate& rCurrent, const WindRadialsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
		std::memcpy(rCurrent.pfBaseIntensities, rPrevious.pfBaseIntensities, rCurrent.iCount * sizeof(rCurrent.pfBaseIntensities[0]));
		std::memcpy(rCurrent.pfBaseSizes, rPrevious.pfBaseSizes, rCurrent.iCount * sizeof(rCurrent.pfBaseSizes[0]));
	}
}

void WindRadialsPostRender::AllocateAndCopy(WindRadialsPostRender& rCurrent, const WindRadialsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void WindRadialsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindRadialsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void WindRadialsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	DestroyExpiredControlled(rFrame.interpolate.windRadials, rFrame.postRender.windRadials, rFrame.interpolate.fCurrentTime,
		[](auto& rI, auto& rPR, int64_t& i)
		{
			DestroyElement(rI, rPR, i, rI.Members(), rPR.Members());
		});
}

} // namespace engine

#endif // BT_CLIENT
