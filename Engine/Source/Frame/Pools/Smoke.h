#pragma once

#include "Frame/Pools/ObjectControllerPool.h"
#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

inline bool gbSmokeClear = true;
inline float gbSmokeSpread = false;

struct PuffInfo
{
	DirectX::XMVECTOR vecPosition {};
	float fIntensity = 0.0f;
	float fArea = 0.0f;
	float fCookie = 0.0f;

	static PuffInfo Lerp(const PuffInfo& rOne, const PuffInfo& rTwo, float fPercent)
	{
		return
		{
			.vecPosition = DirectX::XMVectorLerp(rOne.vecPosition, rTwo.vecPosition, fPercent),
			.fIntensity = (1.0f - fPercent) * rOne.fIntensity + fPercent * rTwo.fIntensity,
			.fArea = (1.0f - fPercent) * rOne.fArea + fPercent * rTwo.fArea,
			.fCookie = (1.0f - fPercent) * rOne.fCookie + fPercent * rTwo.fCookie,
		};
	}

	bool operator==(const PuffInfo& rOther) const = default;
};
struct Puff
{
	bool operator==(const Puff& rOther) const = default;
};
using Puffs = ObjectPool<PuffInfo, Puff, puff_t, kuiMaxPuffs>;
static_assert(std::is_trivially_copyable_v<Puffs>);

template<int64_t CONTROLLER_LERP_SIZE, int64_t CONTROLLER_SIZE>
using PuffControllers = ObjectControllerPool<PuffInfo, Puff, puff_t, kuiMaxPuffs, CONTROLLER_LERP_SIZE, puff_controller_t, CONTROLLER_SIZE>;

struct TrailInfo
{
	DirectX::XMVECTOR vecPosition {};
	float fIntensity = 0.0f;
	float fWidth = 1.0f;

	bool operator==(const TrailInfo& rOther) const = default;
};
struct Trail
{
	float fStartTime {};

	bool operator==(const Trail& rOther) const = default;
};
struct Trails : public ObjectPool<TrailInfo, Trail, trail_t, kuiMaxTrails>
{
	// These are set to current position when gbSmokeClear
	inline static DirectX::XMVECTOR smpVecTrailsPositionPrevious[kuiMaxTrails + 2] {};
	inline static DirectX::XMVECTOR smpVecTrailsPositionSmoothed[kuiMaxTrails + 2] {};

	void Add(trail_t& ruiIndex, float fCurrentTime, const TrailInfo& rTrailInfo)
	{
		bool bNew = ruiIndex == 0;

		ObjectPool::Add(ruiIndex, rTrailInfo);

		if (bNew && ruiIndex != 0) [[unlikely]]
		{
			Trail& rTrail = Get(ruiIndex);
			rTrail.fStartTime = fCurrentTime;
			smpVecTrailsPositionPrevious[ruiIndex] = rTrailInfo.vecPosition;
			smpVecTrailsPositionSmoothed[ruiIndex] = rTrailInfo.vecPosition;
		}
	}
};
static_assert(std::is_trivially_copyable_v<Trails>);

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);
void RenderSmokeMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);

inline constexpr int64_t kiSmokeVersion = 2 + sizeof(Puffs) + sizeof(Trails);

}
