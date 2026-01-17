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
	// Register
	static void Register();

	// Graphics resources
	static void GraphicsResources();

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsInterpolate& rCurrent, const SpaceshipsInterpolate& rPrevious);

	// Interpolate
	static void Update(FrameInterpolate& __restrict rCurrentFrameInterpolate, const Frame& __restrict rPreviousFrame);

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

	// Allocate and copy
	static void AllocateAndCopy(SpaceshipsPostRender& rCurrent, const SpaceshipsPostRender& rPrevious);

	// Post render phases
	static void Update(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PreCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void PostCollision(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void AreaDamage(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame);
	static void Destroy(Frame& __restrict rFrame);
	static void Spawn(Frame& __restrict rFrame);

	SpaceshipFlags_t* __restrict pFlags = nullptr;
	XMVECTOR* __restrict pVecVelocities = nullptr;
	XMVECTOR* __restrict pVecDamageDirections = nullptr;
	float* __restrict pfHealths = nullptr;
	float* __restrict pfDestroyedExplosionTimes = nullptr;
	float* __restrict pfNextBlasterSpawnTimes = nullptr;
	int32_t* __restrict piBlasterSpawns = nullptr;
	auto Members(this auto&& rSelf) { return std::tie(rSelf.pFlags, rSelf.pVecVelocities, rSelf.pVecDamageDirections, rSelf.pfHealths, rSelf.pfDestroyedExplosionTimes, rSelf.pfNextBlasterSpawnTimes, rSelf.piBlasterSpawns); }

	// Utility
	bool operator==(const SpaceshipsPostRender& rOther) const;
	static void AvoidTerrain(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, int64_t iStart, int64_t iEnd);

	// SpawnInfo for spawn parameters
	struct SpawnInfo
	{
		XMVECTOR vecPosition;
		XMVECTOR vecDirection;
	};

	static void Spawn(Frame& __restrict rFrame, const SpawnInfo& rInfo);
};

static_assert(sizeof(shaders::GltfLayout) == engine::kGltfLayoutSize);

} // namespace game
