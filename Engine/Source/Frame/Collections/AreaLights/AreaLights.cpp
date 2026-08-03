#include "AreaLights.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<AreaLightsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<AreaLightsPostRender>;

void AreaLightsInterpolate::AllocateAndCopy(AreaLightsInterpolate& rCurrent, const AreaLightsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void AreaLightsPostRender::AllocateAndCopy(AreaLightsPostRender& rCurrent, const AreaLightsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
