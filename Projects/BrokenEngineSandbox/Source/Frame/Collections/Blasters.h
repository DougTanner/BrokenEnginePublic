#pragma once

#include "Frame/Collision.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Sounds.h"

namespace game
{

struct BlastersInterpolate : public engine::Collection<BlastersInterpolate>
{
	// Types
	struct Type
	{
		XMFLOAT2 f2Size {0.11f, 1.5f};
		uint8_t uiAreaLightTypeIndex = 0;
	};

	static inline std::vector<Type> sTypes;

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	uint8_t* __restrict puiTypeIndices = nullptr;
	XMVECTOR* __restrict pVecPositions = nullptr;
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.puiTypeIndices, rSelf.pVecPositions, rSelf.puiAreaLights); }

	// Utility
	bool operator==(const BlastersInterpolate& rOther) const;
};

enum class BlasterFlags : uint8_t
{
	kDestroy       = 0x01,
	kCollidePlayer = 0x02,
};
using BlasterFlags_t = common::Flags<BlasterFlags>;

struct BlastersPostRender : public engine::Collection<BlastersPostRender>
{
	// Update
	static void Update(BlastersPostRender& __restrict rCurrent, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, FXMVECTOR vecPosition, FXMVECTOR vecVelocity, uint8_t uiTypeIndex, BlasterFlags_t flags = {});
	static void Destroy(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	float* __restrict pfPitches = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.puiSounds, rSelf.pfPitches); }

	// Utility
	bool operator==(const BlastersPostRender& rOther) const;
};

} // namespace game
