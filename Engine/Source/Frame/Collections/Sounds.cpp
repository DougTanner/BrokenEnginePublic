#include "Sounds.h"

#include "Frame/Frame.h"

namespace engine
{

void SoundsInterpolate::Register()
{
}

void SoundsInterpolate::AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void SoundsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	SoundsInterpolate& rSounds = rFrameInterpolate.sounds;
	int64_t iIndex = rSounds.IdToIndex(id);

	rSounds.pVecPositions[iIndex] = rData.vecPosition;
	rSounds.pVecVelocities[iIndex] = rData.vecVelocity;
	rSounds.puiCrcs[iIndex] = rData.uiCrc;
	rSounds.pfVolumes[iIndex] = rData.fVolume;
	rSounds.pfPitches[iIndex] = rData.fPitch;
	rSounds.pfFadeOutTimes[iIndex] = rData.fFadeOutTime;
}

void SoundsPostRender::AllocateAndCopy(SoundsPostRender& rCurrent, const SoundsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void SoundsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

void SoundsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsPostRender::Add(game::Frame& __restrict rFrame, sound_t& rId)
{
	ASSERT(!rId.IsValid());

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void SoundsPostRender::Remove(game::Frame& __restrict rFrame, sound_t& rId)
{
	ASSERT(rId.IsValid());

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void SoundsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void SoundsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
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

} // namespace engine
