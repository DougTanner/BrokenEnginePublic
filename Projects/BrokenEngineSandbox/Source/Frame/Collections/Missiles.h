#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Renderable.h"
#include "Frame/Collections/AreaLights.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Sounds.h"
#include "Frame/Collections/Trails.h"
#include "Shaders/ShaderLayouts.h"

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

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

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
	static constexpr int64_t kiVersion = 1;

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime, FXMVECTOR vecPosition, FXMVECTOR vecDirection, FXMVECTOR vecVelocity, target_t uiTarget, float fAcceleration, MissileFlags_t flags);
	static void Explode(Frame& __restrict rFrame, int64_t i, bool bDirectional);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	MissileFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecExplosionDirections = nullptr;
	target_t* __restrict puiTargets = nullptr;
	float* __restrict pfExplosionRadii = nullptr;
	float* __restrict pfTimes = nullptr;
	float* __restrict pfDeltaRotationDelays = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfExaustDelays = nullptr;
	float* __restrict pfNextJitter = nullptr;
	float* __restrict pfDeltaRotationMax = nullptr;
	float* __restrict pfExplosionTimes = nullptr;
	float* __restrict pfAccelerations = nullptr;
	float* __restrict pfPitches = nullptr;
	engine::sound_t* __restrict puiSounds = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecExplosionDirections, rSelf.puiTargets, rSelf.pfExplosionRadii, rSelf.pfTimes, rSelf.pfDeltaRotationDelays, rSelf.pfDeltaRotations, rSelf.pfExaustDelays, rSelf.pfNextJitter, rSelf.pfDeltaRotationMax, rSelf.pfExplosionTimes, rSelf.pfAccelerations, rSelf.pfPitches, rSelf.puiSounds); }

	// Utility
	bool operator==(const MissilesPostRender& rOther) const;
};

static_assert(sizeof(shaders::GltfLayout) == engine::kGltfLayoutSize);

} // namespace game
