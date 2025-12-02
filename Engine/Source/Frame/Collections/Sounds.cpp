#include "Sounds.h"

#include "Frame/Frame.h"

namespace engine
{

void SoundsInterpolate::Update([[maybe_unused]] SoundsInterpolate& __restrict rCurrent, [[maybe_unused]] const SoundsInterpolate& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());
}

void SoundsPostRender::Update([[maybe_unused]] SoundsPostRender& __restrict rCurrent, [[maybe_unused]] const SoundsPostRender& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		sound_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

sound_t XM_CALLCONV SoundsPostRender::Add(game::Frame& __restrict rFrame,
                                          common::crc_t uiCrc, float fVolume, float fPitch, float fFadeOutTime,
                                          FXMVECTOR vecPosition, FXMVECTOR vecVelocity)
{
	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);

	// Initialize sound data
	rInterpolate.puiCrcs[uiSpawnIndex] = uiCrc;
	rInterpolate.pfVolumes[uiSpawnIndex] = fVolume;
	rInterpolate.pfPitches[uiSpawnIndex] = fPitch;
	rInterpolate.pfFadeOutTimes[uiSpawnIndex] = fFadeOutTime;
	rInterpolate.pVecPositions[uiSpawnIndex] = vecPosition;
	rInterpolate.pVecVelocities[uiSpawnIndex] = vecVelocity;

	rPostRender.puiIds[uiSpawnIndex] = newId;

	return newId;
}

void SoundsPostRender::Remove(game::Frame& __restrict rFrame, sound_t id)
{
	if (!id.IsValid())
	{
		return;
	}

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
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
