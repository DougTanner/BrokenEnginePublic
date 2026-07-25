#pragma once

#if defined(BT_CLIENT)

#include "Frame/Collections/Collection.h"
#include "Frame/GridCoord.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

}

namespace engine
{

struct FrameStaticData;

struct SoundsInterpolate : public Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>
{
	// Allocate and copy
	static void AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious);

	// SyncData for parent-provided values
	struct SyncData
	{
		XMVECTOR vecPosition;
		XMVECTOR vecVelocity;
		common::crc_t uiCrc;
		float fVolume;
		float fPitch;
		float fFadeOutTime;
	};

	// Sync owned sound with parent-provided data
	static void Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData);

	// Update
	static void Update(game::FrameInterpolate& __restrict rFrameInterpolate, const game::Frame& __restrict rPreviousFrame);

	// Member arrays (SOA)
	common::crc_t* __restrict puiCrcs = nullptr;
	float* __restrict pfVolumes = nullptr;
	float* __restrict pfPitches = nullptr;
	float* __restrict pfFadeOutTimes = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;

	auto Members(this auto&& rSelf)
	{
		return std::tie(rSelf.puiCrcs, rSelf.pfVolumes, rSelf.pfPitches,
		                rSelf.pfFadeOutTimes, rSelf.pVecPositions, rSelf.pVecVelocities);
	}

};
using sound_t = SoundsInterpolate::id_t;

struct SoundsPostRender : public Collection<SoundsPostRender>
{
	// Allocate and copy
	static void AllocateAndCopy(SoundsPostRender& rCurrent, const SoundsPostRender& rPrevious);

	// Update
	static void Update(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, const FrameStaticData& rStaticData);

	// Add sound
	static void Add(game::Frame& __restrict rFrame, sound_t& rId);

	// Remove sound by ID
	static void Remove(game::Frame& __restrict rFrame, sound_t& rId);

	sound_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

};

extern template struct Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>;
extern template struct Collection<SoundsPostRender>;

} // namespace engine

#endif // BT_CLIENT
