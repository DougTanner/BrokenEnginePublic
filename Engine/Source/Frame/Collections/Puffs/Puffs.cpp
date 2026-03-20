#include "Puffs.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<PuffsInterpolate>;
template struct Collection<PuffsPostRender>;

void PuffsInterpolate::Register()
{
}

void PuffsInterpolate::AllocateAndCopy(PuffsInterpolate& rCurrent, const PuffsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
	}
}

void PuffsPostRender::AllocateAndCopy(PuffsPostRender& rCurrent, const PuffsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void PuffsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PuffsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PuffsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);

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
