#pragma once

#include "Frame/Collections/Collections.h"
#include "Frame/Pools/Explosions.h"
#include "Frame/Pools/Pushers.h"
#include "Frame/Pools/Targets.h"

namespace game
{

enum class SpaceshipFlags : uint8_t
{
	kFleePlayer           = 0x01,
	kExploding            = 0x02,
	kReturnToIslandCenter = 0x04,
};
using SpaceshipFlags_t = common::Flags<SpaceshipFlags>;

struct alignas(64) Spaceships
{
	static constexpr int64_t kiMax = 1024;

	static constexpr float kfBlastersSpeed = 65.0f;
	static constexpr float kfFreezeTimeAreaDamage = 0.075f;
	static constexpr float kfBurnSize = 0.9f;

	// Interpolate
	int64_t iCount = 0;
	int64_t iKilled = 0;

	alignas(64) SpaceshipFlags_t pFlags[kiMax] {};
	alignas(64) DirectX::XMVECTOR pVecPositions[kiMax] {};
	alignas(64) DirectX::XMVECTOR pVecDirections[kiMax] {};
	alignas(64) engine::pusher_t puiPushers[kiMax] {};
	alignas(64) engine::target_t puiTargets[kiMax] {};
	alignas(64) engine::trail_t puiDamageTrails[kiMax] {};
	alignas(64) engine::billboard_t puiBillboards[kiMax] {};
	alignas(64) float pfDestroyedTimes[kiMax] {};

	// Post render
	alignas(64) DirectX::XMVECTOR pVecVelocities[kiMax] {};
	alignas(64) float pfDeltaRotations[kiMax] {};
	alignas(64) float pfHealths[kiMax] {};
	alignas(64) float pfFreezeTimes[kiMax] {};
	alignas(64) float pfDestroyedExplosionTimes[kiMax] {};
	alignas(64) float pfNextBlasterSpawnTimes[kiMax] {};
	alignas(64) int32_t piBlasterSpawns[kiMax] {};

	// Utility
	bool operator==(const Spaceships& rOther) const;
	void Copy(int64_t iDestIndex, int64_t iSrcIndex);
		
	// Update
	static void Global(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void Interpolate(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInputHeld& __restrict rFrameInputHeld, float fDeltaTime);
	static void PostRender(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void PostRenderAvoidTerrain(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime, int64_t iStart, int64_t iEnd);
	static void PostRenderPushers(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime, int64_t iStart, int64_t iEnd);
	static void XM_CALLCONV Explode(Frame& __restrict rFrame, int64_t i, DirectX::FXMVECTOR vecDirection = DirectX::XMVectorZero());
	static void Collide(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void XM_CALLCONV Spawn(Frame& __restrict rFrame, DirectX::FXMVECTOR vecPosition, DirectX::FXMVECTOR vecDirection);
	static void Spawn(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);
	static void Destroy(Frame& __restrict rFrame, const Frame& __restrict rPreviousFrame, const FrameInput& __restrict rFrameInput, float fDeltaTime);

	// Render
	static void RenderGlobal(int64_t iCommandBuffer, const Frame& __restrict rFrame);
	static void RenderMain(int64_t iCommandBuffer, const Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<Spaceships>);
inline constexpr int64_t kiSpaceshipsVersion = 5 + sizeof(Spaceships);

} // namespace game
