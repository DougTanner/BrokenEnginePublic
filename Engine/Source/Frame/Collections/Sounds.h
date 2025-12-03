#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct SoundsInterpolate : public Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>
{
	// Update
	static void Update(SoundsInterpolate& __restrict rCurrent, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

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

	// Utility
	bool operator==(const SoundsInterpolate& rOther) const;
};
using sound_t = SoundsInterpolate::id_t;

struct SoundsPostRender : public Collection<SoundsPostRender>
{
	// Update
	static void Update(SoundsPostRender& __restrict rCurrent, const SoundsPostRender& __restrict rPrevious);

	// Add sound (returns ID for external tracking)
	static sound_t XM_CALLCONV Add(game::Frame& __restrict rFrame,
	                               common::crc_t uiCrc, float fVolume, float fPitch, float fFadeOutTime,
	                               FXMVECTOR vecPosition, FXMVECTOR vecVelocity);

	// Remove sound by ID
	static void Remove(game::Frame& __restrict rFrame, sound_t id);

	sound_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const SoundsPostRender& rOther) const;
};

} // namespace engine
