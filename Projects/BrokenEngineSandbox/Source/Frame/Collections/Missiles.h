#pragma once

#include "Frame/Alignments.h"
#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Renderable.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/Trails.h"

#include "Frame/Collections/Targets.h"

namespace game
{

struct Frame;
struct FrameInterpolate;

enum class MissileFlags : uint8_t
{
	kDestroy   = 0x01,
	kExploding = 0x02,
	kDirectional = 0x04,
	kTargetPlayer = 0x08,
	kTargetEnemy  = 0x10,
};
using MissileFlags_t = common::Flags<MissileFlags>;

struct MissilesInterpolate : public engine::Collection<MissilesInterpolate>,
                             public engine::Renderable<MissilesInterpolate, "Missiles", {engine::RenderableFlags::kGltf, engine::RenderableFlags::kGltfShadow}, data::kGltfaim9_missilescenegltfCrc, data::kGltfaim9_missilescenegltfGLTF_MODELCrc>
{
	// Called on Game creation
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(MissilesInterpolate& rCurrent, const MissilesInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	engine::area_lights_t* __restrict puiAreaLights = nullptr;
	engine::pusher_t* __restrict puiPushers = nullptr;
	engine::trails_t* __restrict puiTrails = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.puiAreaLights, rSelf.puiPushers, rSelf.puiTrails, rSelf.pfDestroyedTimes); }

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const MissilesInterpolate& rOther) const;
};

struct MissilesPostRender : public engine::Collection<MissilesPostRender>
{
	static constexpr int64_t kiVersion = 3;

	// Allocate and copy
	static void AllocateAndCopy(MissilesPostRender& rCurrent, const MissilesPostRender& rPrevious);

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);
	static void Explode(Frame& __restrict rFrame, int64_t i, bool bDirectional);

	MissileFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecExplosionDirections = nullptr;
	XMVECTOR* __restrict pVecStoredDirections = nullptr;
	target_t* __restrict puiTargets = nullptr;
	float* __restrict pfExplosionRadii = nullptr;
	float* __restrict pfTimes = nullptr;
	float* __restrict pfDeltaRotationDelays = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfExaustDelays = nullptr;
	float* __restrict pfNextJitter = nullptr;
	float* __restrict pfDeltaRotationMax = nullptr;
	float* __restrict pfAccelerations = nullptr;
	float* __restrict pfPitches = nullptr;
	float* __restrict pfExhaustLengths = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	engine::alignment_t* __restrict pAlignments = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecExplosionDirections, rSelf.pVecStoredDirections, rSelf.puiTargets, rSelf.pfExplosionRadii, rSelf.pfTimes, rSelf.pfDeltaRotationDelays, rSelf.pfDeltaRotations, rSelf.pfExaustDelays, rSelf.pfNextJitter, rSelf.pfDeltaRotationMax, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.pfExhaustLengths, rSelf.puiSounds, rSelf.pAlignments); }

	// Utility
	bool operator==(const MissilesPostRender& rOther) const;

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
		XMVECTOR vecVelocity;
		XMVECTOR vecStoredDirection;
		target_t uiTarget;
		float fAcceleration;
		MissileFlags_t flags;
		engine::alignment_t alignment {};
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

static_assert(sizeof(shaders::GltfLayout) == engine::kGltfLayoutSize);

} // namespace game
