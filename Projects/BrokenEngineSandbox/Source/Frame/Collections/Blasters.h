#pragma once

#include "Frame/Collision.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Sounds.h"

namespace game
{

struct BlastersType
{
	XMFLOAT2 f2Size {0.11f, 1.5f};
	uint8_t uiAreaLightTypeIndex = 0;
};

struct BlastersInterpolate : public engine::Collection<BlastersInterpolate>,
                             public engine::TypeRegistry<BlastersType>
{
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources() {}

	// Allocate and copy
	static void AllocateAndCopy(BlastersInterpolate& rCurrent, const BlastersInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

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
	// Allocate and copy
	static void AllocateAndCopy(BlastersPostRender& rCurrent, const BlastersPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	BlasterFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	float* __restrict pfPitches = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.puiSounds, rSelf.pfPitches); }

	// Utility
	bool operator==(const BlastersPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecVelocity;
		uint8_t uiTypeIndex;
		BlasterFlags_t flags {};
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

} // namespace game
