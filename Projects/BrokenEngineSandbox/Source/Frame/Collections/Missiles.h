#pragma once

#include "Ui/Wrapper.h"
#include "Frame/Collections/Collections.h"
#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/ObjectPool.h"
#include "Frame/Pools/Smoke.h"
#include "Frame/Pools/Sounds.h"
#include "Frame/Pools/Targets.h"

namespace game
{

enum class MissileFlags : uint8_t
{
	kDestroy   = 0x01,

	kExploding = 0x02,
		kDirectional = 0x04,

	kTargetPlayer = 0x08,
	kTargetEnemy  = 0x10,
};
using MissileFlags_t = common::Flags<MissileFlags>;

inline constexpr int64_t kiMaxSpawnMissiles = 64;

struct SpawnMissile
{
	MissileFlags_t flags;
	DirectX::XMVECTOR vecPosition {};
	DirectX::XMVECTOR vecDirection {};
	DirectX::XMVECTOR vecVelocity {};
	engine::target_t uiTarget = 0;
	float fExplosionRadius = 1.0f;
	float fAcceleration = 0.0f;

	bool operator==(const SpawnMissile& rOther) const = default;
};

// Spawnable requires 1. operator== 2. Main() 3. Spawn()
struct alignas(64) Missiles : public engine::Spawnable<SpawnMissile, kiMaxSpawnMissiles>
{
	static constexpr int64_t kiMax = 256;

	// Interpolate
	int64_t iCount = 0;

	alignas(64) DirectX::XMVECTOR pVecPositions[kiMax] {};
	alignas(64) DirectX::XMVECTOR pVecDirections[kiMax] {};
	alignas(64) engine::area_light_t puiAreaLights[kiMax] {};
	alignas(64) engine::pusher_t puiPushers[kiMax] {};
	alignas(64) engine::trail_t puiTrails[kiMax] {};
	alignas(64) engine::target_t puiSelfTargets[kiMax] {};
	alignas(64) float pfDestroyedTimes[kiMax] {};

	// Post render
	alignas(64) MissileFlags_t pFlags[kiMax] {};
	alignas(64) DirectX::XMVECTOR pVecVelocities[kiMax] {};
	alignas(64) DirectX::XMVECTOR pVecExplosionDirections[kiMax] {};
	alignas(64) engine::target_t puiTargets[kiMax] {};
	alignas(64) float pfExplosionRadii[kiMax] {};
	alignas(64) float pfTimes[kiMax] {};
	alignas(64) float pfDeltaRotationDelays[kiMax] {};
	alignas(64) float pfDeltaRotations[kiMax] {};
	alignas(64) float pfExaustDelays[kiMax] {};
	alignas(64) float pfNextJitter[kiMax] {};
	alignas(64) float pfDeltaRotationMax[kiMax] {};
	alignas(64) float pfExplosionTimes[kiMax] {};
	alignas(64) float pfAccelerations[kiMax] {};
	alignas(64) float pfPitches[kiMax] {};
	alignas(64) engine::sound_t puiSounds[kiMax] {};

	// Utility
	bool operator==(const Missiles& rOther) const;
	void Copy(int64_t iDestIndex, int64_t iSrcIndex);

	// Update
	static void Global(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Explode(Frame& __restrict rFrame, const FrameInput& __restrict rFrameInput, int64_t i, bool bDirectional);
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, int64_t i);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderGlobal(int64_t iCommandBuffer, const Frame& __restrict rFrame);
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Missiles>);
inline constexpr int64_t kiMissilesVersion = 5 + sizeof(Missiles);

} // namespace game
