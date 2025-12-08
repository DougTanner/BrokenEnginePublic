#pragma once

#include "Frame/Collections/Collection.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

}

namespace engine
{

struct SoundsInterpolate : public Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources() {}

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
	static void Update(SoundsPostRender& __restrict rCurrent, const SoundsPostRender& __restrict rPrevious, float fDeltaTime);
	static void PreCollision(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	// Add sound
	static void Add(game::Frame& __restrict rFrame, sound_t& rId);

	// Remove sound by ID
	static void Remove(game::Frame& __restrict rFrame, sound_t& rId);

	sound_t* __restrict puiIds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiIds); }

	// Utility
	bool operator==(const SoundsPostRender& rOther) const;
};

} // namespace engine
