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

// DT: Gamelogic
enum class PusherFlags : uint8_t
{
	kTypeNone    = 0x00,
	kTypeDefault = 0x01,
	kTypeMines   = 0x02,
};
using PusherFlags_t = common::Flags<PusherFlags>;

struct PusherInfo
{
	DirectX::XMFLOAT2 f2Position {};
	float fRadius = 0.0f;
	float fIntensity = 0.0f;
	float fPower = 0.0f;
	PusherFlags_t flags {PusherFlags::kTypeDefault};

	bool operator==(const PusherInfo& rOther) const = default;
};
struct Pusher
{
	bool operator==(const Pusher& rOther) const = default;
};
struct Pushers : public ObjectPool<PusherInfo, Pusher, pusher_t, kuiMaxPushers>
{
	void SetupZones(game::Frame& __restrict rFrame);

	DirectX::XMVECTOR XM_CALLCONV ApplyPush(DirectX::FXMVECTOR vecPosition, pusher_t uiIgnorePusher = 0, PusherFlags_t includeFlags = PusherFlags::kTypeDefault, PusherFlags_t excludeFlags = PusherFlags::kTypeMines);
};
static_assert(std::is_trivially_copyable_v<Pushers>);

inline constexpr int64_t kiPushersVersion = 1 + sizeof(Pushers);

}
