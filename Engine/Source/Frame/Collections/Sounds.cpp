#include "Sounds.h"

#include "Frame/Frame.h"

namespace engine
{

void SoundsInterpolate::Register()
{
}

void SoundsInterpolate::AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void SoundsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, static_cast<size_t>(rCurrent.iCount) * sizeof(sound_t));
	}
}

void SoundsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void SoundsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

void SoundsPostRender::Add(game::Frame& __restrict rFrame, sound_t& rId)
{
	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;
}

void SoundsPostRender::Remove(game::Frame& __restrict rFrame, sound_t& rId)
{
	if (!rId.IsValid())
	{
		return;
	}

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
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
