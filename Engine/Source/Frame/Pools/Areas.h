#pragma once

#include "Frame/Pools/ObjectPool.h"

namespace engine
{

struct AreaInfo
{
	common::AreaVertices areaVertices;

	float fDamagePerSecond = 0.0f;

	bool operator==(const AreaInfo& rOther) const = default;
};
struct Area
{
	bool operator==(const Area& rOther) const = default;
};
class Areas : public ObjectPool<AreaInfo, Area, area_t, kuiMaxAreas>
{
};
static_assert(std::is_trivially_copyable_v<Areas>);

inline constexpr int64_t kiAreasVersion = 1 + sizeof(Areas);

}
