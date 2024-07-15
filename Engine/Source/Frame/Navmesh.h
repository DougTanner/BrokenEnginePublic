#pragma once

#include "Frame/Pools/PoolConfig.h"

namespace game
{

struct Frame;
struct FrameInput;
struct FrameInputHeld;

} // namespace game

namespace engine
{

struct alignas(64) Navmesh
{
	static constexpr int64_t kiGrid = 16;
	static constexpr float kfHeight = 2.0f;

	inline static uint8_t smppuiPlayerDistances[kiGrid][kiGrid] {};

	// Post render
	int64_t iLastPlayerX = 0;
	int64_t iLastPlayerY = 0;
	alignas(64) bool ppbGridNavigable[kiGrid][kiGrid] {};
#if defined(ENABLE_NAVMESH_DISPLAY)
	alignas(64) billboard_t ppuiBillboards[kiGrid][kiGrid] {};
	billboard_t uiBillboard = 0;
#endif

	// Utility
	bool operator==(const Navmesh& rOther) const;

	static void SetupGrid(DirectX::XMFLOAT4 f4GlobalVisibleArea, Navmesh& __restrict rNavmesh);
	static void SetupPlayerDistances(game::Frame& __restrict rFrame, const game::Frame& __restrict rPreviousFrame);
	static DirectX::XMVECTOR XM_CALLCONV NodeToPlayer(game::Frame& __restrict rFrame, DirectX::XMVECTOR vecPosition);
};
static_assert(std::is_trivially_copyable_v<Navmesh>);
inline constexpr int64_t kiNavmeshVersion = 1 + sizeof(Navmesh);

} // namespace engine
