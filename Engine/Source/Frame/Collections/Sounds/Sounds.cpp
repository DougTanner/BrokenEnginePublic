#include "Sounds.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<SoundsPostRender>;

void SoundsInterpolate::Register()
{
}

void SoundsInterpolate::AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiCrcs, rPrevious.puiCrcs, rCurrent.iCount * sizeof(rCurrent.puiCrcs[0]));
		std::memcpy(rCurrent.pfVolumes, rPrevious.pfVolumes, rCurrent.iCount * sizeof(rCurrent.pfVolumes[0]));
		std::memcpy(rCurrent.pfPitches, rPrevious.pfPitches, rCurrent.iCount * sizeof(rCurrent.pfPitches[0]));
		std::memcpy(rCurrent.pfFadeOutTimes, rPrevious.pfFadeOutTimes, rCurrent.iCount * sizeof(rCurrent.pfFadeOutTimes[0]));
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.pVecVelocities, rPrevious.pVecVelocities, rCurrent.iCount * sizeof(rCurrent.pVecVelocities[0]));
	}
}

void SoundsPostRender::AllocateAndCopy(SoundsPostRender& rCurrent, const SoundsPostRender& rPrevious)
{
	AllocateAndCopyIds(rCurrent, rPrevious);
}

void SoundsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	// Owned objects are transferred by their parent
}

void SoundsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace engine

#endif // BT_CLIENT
