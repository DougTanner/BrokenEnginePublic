#pragma once

#include "Frame/UpdateList.h"
#include "Frame/Pools/ObjectPool.h"

namespace game
{

struct Frame;

}

namespace engine
{

struct HexShieldInfo
{
	shaders::HexShieldLayout hexShieldLayout {};

	bool operator==(const HexShieldInfo& rOther) const = default;
};
struct HexShield
{
	bool operator==(const HexShield& rOther) const = default;
};
class HexShields : public UpdateList, public ObjectPool<HexShieldInfo, HexShield, hex_shield_t, kuiMaxHexShields>
{
public:

	static void RenderMain(int64_t iCommandBuffer, const game::Frame& __restrict rFrame);
};
static_assert(std::is_trivially_copyable_v<HexShields>);

inline constexpr int64_t kiHexShieldsVersion = 1 + sizeof(HexShields);

}
