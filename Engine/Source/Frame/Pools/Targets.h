#pragma once

#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

enum class TargetFlags : uint8_t
{
	kDestination = 0x01,
	kSubscriber  = 0x02,

	// DT: GAMELOGIC
	kTargetIsPlayer = 0x04,
	kTargetIsEnemy  = 0x08,
};
using TargetFlags_t = common::Flags<TargetFlags>;

using subscriber_t = uint8_t;

struct TargetInfo
{
	TargetFlags_t flags;
	DirectX::XMVECTOR vecPosition {};
	float fBillboardSize = 0.055f;

	bool operator==(const TargetInfo& rOther) const = default;
};
struct Target
{
	subscriber_t uiSubscribers = 0;
	int64_t iBillboard = 0;
	billboard_t uiBillboard = 0;

	bool operator==(const Target& rOther) const = default;
};
struct Targets : public ObjectPool<TargetInfo, Target, target_t, kuiMaxTargets>
{
	static void Interpolate(game::Frame& __restrict rFrame);

	void Remove(target_t& __restrict ruiIndex) = delete;
	void Remove(game::Frame& __restrict rFrame, target_t& __restrict ruiIndex, TargetFlags_t flags);
};
static_assert(std::is_trivially_copyable_v<Targets>);

inline constexpr int64_t kiTargetsVersion = 1 + sizeof(Targets);

}
