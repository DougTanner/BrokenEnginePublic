#include "Sounds.h"

#include "Frame/Frame.h"

namespace engine
{

void SoundsInterpolate::Update([[maybe_unused]] SoundsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
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

void SoundsPostRender::Update([[maybe_unused]] SoundsPostRender& __restrict rCurrent, [[maybe_unused]] const SoundsPostRender& __restrict rPrevious)
{
	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		sound_t id = rPrevious.puiIds[i];

		// Save
		rCurrent.puiIds[i] = id;
	}
}

void SoundsPostRender::Add(game::Frame& __restrict rFrame, sound_t& rId)
{
	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = engine::AddIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.puiCrcs[uiSpawnIndex] = 0;
	rInterpolate.pfVolumes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfPitches[uiSpawnIndex] = 0.0f;
	rInterpolate.pfFadeOutTimes[uiSpawnIndex] = 0.0f;
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorZero();
	rInterpolate.pVecVelocities[uiSpawnIndex] = XMVectorZero();
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
