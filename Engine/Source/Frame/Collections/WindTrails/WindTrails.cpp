#include "WindTrails.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<WindTrailsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<WindTrailsPostRender>;

void WindTrailsInterpolate::AllocateAndCopy(WindTrailsInterpolate& rCurrent, const WindTrailsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void WindTrailsPostRender::AllocateAndCopy(WindTrailsPostRender& rCurrent, const WindTrailsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
