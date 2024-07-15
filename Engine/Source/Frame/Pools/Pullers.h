#pragma once

#include "Frame/Pools/Lighting.h"
#include "Frame/Pools/ObjectPool.h"
#include "Frame/Pools/Smoke.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct PullerInfo
{
	DirectX::XMFLOAT2 f2Position {};
	float fRadius = 0.0f;
	float fIntensity = 0.0f;
	float fPower = 0.0f;

	bool operator==(const PullerInfo& rOther) const = default;
};
struct Puller
{
	bool operator==(const Puller& rOther) const = default;
};
struct Pullers : public ObjectPool<PullerInfo, Puller, puller_t, kuiMaxPullers>
{
	void SetupZones(game::Frame& __restrict rFrame);

	DirectX::XMVECTOR XM_CALLCONV ApplyPull(DirectX::FXMVECTOR vecPosition);
};
static_assert(std::is_trivially_copyable_v<Pullers>);

inline constexpr int64_t kiPullersVersion = 1 + sizeof(Pullers);

}
