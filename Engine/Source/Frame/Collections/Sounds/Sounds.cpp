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
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void SoundsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void SoundsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void SoundsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool SoundsInterpolate::operator==(const SoundsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiCrcs[i], rOther.puiCrcs[i]);
		bEqual &= common::BreakOnNotEqual(pfVolumes[i], rOther.pfVolumes[i]);
		bEqual &= common::BreakOnNotEqual(pfPitches[i], rOther.pfPitches[i]);
		bEqual &= common::BreakOnNotEqual(pfFadeOutTimes[i], rOther.pfFadeOutTimes[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(pVecVelocities[i], rOther.pVecVelocities[i]);
	}

	return bEqual;
}

bool SoundsPostRender::operator==(const SoundsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

void SoundsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace engine

#endif // BT_CLIENT
