#include "Sounds.h"

#if defined(BT_CLIENT)

namespace engine
{

void SoundsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void SoundsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	SoundsInterpolate& rSounds = rFrameInterpolate.sounds;
	int64_t iIndex = rSounds.IdToIndex(id);

	rSounds.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rSounds.pVecVelocities[iIndex] = rData.vecVelocity;
	rSounds.puiCrcs[iIndex] = rData.uiCrc;
	rSounds.pfVolumes[iIndex] = rData.fVolume;
	rSounds.pfPitches[iIndex] = rData.fPitch;
	rSounds.pfFadeOutTimes[iIndex] = rData.fFadeOutTime;
}

void SoundsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsPostRender::Add(game::Frame& __restrict rFrame, sound_t& rId)
{
	ASSERT(!rId.IsValid());

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [iSpawnIndex, newId] = AddGeneratedIndexableElement(rInterpolate, rPostRender, [&rFrame]()
	{
		return sound_t {uuid_t {rFrame.postRender.GenerateSoundUuid()}};
	});
	rId = newId;
	rPostRender.puiIds[iSpawnIndex] = newId;
	ZeroMemberRow(iSpawnIndex, rInterpolate.Members());
}

void SoundsPostRender::Remove(game::Frame& __restrict rFrame, sound_t& rId)
{
	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	RemoveIndexableElementAndClearHandle(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
}

} // namespace engine

#endif // BT_CLIENT
