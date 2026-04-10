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

void SoundsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsPostRender::Add(game::Frame& __restrict rFrame, sound_t& rId)
{
	ASSERT(!rId.IsValid());

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	// Heap: idToIndexMap[] may allocate a new node for the ID-to-index entry
	ScopedSuppressAllocationTracking suppressAllocationTracking;
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);
	sound_t newId {uuid_t{rFrame.postRender.GenerateSoundUuid()}};
	rInterpolate.idToIndexMap.insert_or_assign(newId, iSpawnIndex);
	rId = newId;
	rPostRender.puiIds[iSpawnIndex] = newId;
	rInterpolate.puiCrcs[iSpawnIndex] = {};
	rInterpolate.pfVolumes[iSpawnIndex] = 0.0f;
	rInterpolate.pfPitches[iSpawnIndex] = 0.0f;
	rInterpolate.pfFadeOutTimes[iSpawnIndex] = 0.0f;
	rInterpolate.pVecPositions[iSpawnIndex] = XMVectorZero();
	rInterpolate.pVecVelocities[iSpawnIndex] = XMVectorZero();
}

void SoundsPostRender::Remove(game::Frame& __restrict rFrame, sound_t& rId)
{
	ASSERT(rId.IsValid());

	SoundsInterpolate& rInterpolate = rFrame.interpolate.sounds;
	SoundsPostRender& rPostRender = rFrame.postRender.sounds;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void SoundsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine

#endif // BT_CLIENT
