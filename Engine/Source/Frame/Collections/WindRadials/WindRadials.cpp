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
	WindRadialsInterpolate& rInterpolate = rFrame.interpolate.windRadials;
	WindRadialsPostRender& rPostRender = rFrame.postRender.windRadials;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);

		// Skip if not auto-destroy
		if (!rController.bDestroysSelf)
		{
			continue;
		}

		// Check if animation has expired
		float fStartTime = rInterpolate.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;
		bool bExpired = fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1];

		if (bExpired) [[unlikely]]
		{
			DestroyElement(rInterpolate, rPostRender, i, rInterpolate.Members(), rPostRender.Members());
		}
	}
}

} // namespace engine

#endif // BT_CLIENT
