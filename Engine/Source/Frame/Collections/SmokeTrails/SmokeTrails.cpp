#include "SmokeTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<SmokeTrailsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<SmokeTrailsPostRender>;

void SmokeTrailsInterpolate::AllocateAndCopy(SmokeTrailsInterpolate& rCurrent, const SmokeTrailsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void SmokeTrailsPostRender::AllocateAndCopy(SmokeTrailsPostRender& rCurrent, const SmokeTrailsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
