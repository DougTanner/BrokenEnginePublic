#include "Billboards.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<BillboardsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<BillboardsPostRender>;

void BillboardsInterpolate::AllocateAndCopy(BillboardsInterpolate& rCurrent, const BillboardsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void BillboardsPostRender::AllocateAndCopy(BillboardsPostRender& rCurrent, const BillboardsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
