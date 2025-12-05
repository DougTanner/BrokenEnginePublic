#pragma once

#include "Frame/Collections/Collection.h"
#include "Frame/Collections/Pushers.h"
#include "Frame/Collections/Targets.h"
#include "Shaders/ShaderLayouts.h"

namespace game
{

struct SpaceshipsInterpolate : public engine::Collection<SpaceshipsInterpolate>,
                               public engine::Renderable<SpaceshipsInterpolate, "Spaceships", {engine::RenderableFlags::kGltf, engine::RenderableFlags::kGltfShadow}, data::kGltfSpaceshipscenegltfCrc, data::kGltfSpaceshipscenegltfGLTF_MODELCrc>
{

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame, float fDeltaTime);

	XMVECTOR* __restrict pVecPositions = nullptr;
	XMVECTOR* __restrict pVecDirections = nullptr;
	float* __restrict pfDestroyedTimes = nullptr;
	engine::pusher_t* __restrict puiPushers = nullptr;
	target_t* __restrict puiTargets = nullptr;
	float* __restrict pfDeltaRotations = nullptr;
	float* __restrict pfFreezeTimes = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pVecPositions, rSelf.pVecDirections, rSelf.pfDestroyedTimes, rSelf.puiPushers, rSelf.puiTargets, rSelf.pfDeltaRotations, rSelf.pfFreezeTimes); }

	// Render
	static void Render(const FrameInterpolate& __restrict rFrameInterpolate, int64_t iCommandBuffer);

	// Utility
	bool operator==(const SpaceshipsInterpolate& rOther) const;
};

enum class SpaceshipFlags : uint8_t
{
	kFleePlayer           = 0x01,
	kExploding            = 0x02,
	kReturnToIslandCenter = 0x04,
};
using SpaceshipFlags_t = common::Flags<SpaceshipFlags>;

struct SpaceshipsPostRender : public engine::Collection<SpaceshipsPostRender>
{
	static constexpr int64_t kiVersion = 3;

	// Update
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void AvoidTerrain(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, int64_t iStart, int64_t iEnd);
	static void PreCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void PostCollision(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void AreaDamage(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime, FXMVECTOR vecPosition, FXMVECTOR vecDirection);
	static void Destroy(Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame, float fDeltaTime);

	SpaceshipFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	float* __restrict pfHealths = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfNextBlasterSpawnTimes = nullptr;
	int32_t* __restrict piBlasterSpawns = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pfHealths, rSelf.pfDestroyedExplosionTimes, rSelf.pfNextBlasterSpawnTimes, rSelf.piBlasterSpawns); }

	// Utility
	bool operator==(const SpaceshipsPostRender& rOther) const;
};

static_assert(sizeof(shaders::GltfLayout) == engine::kGltfLayoutSize);

} // namespace game
