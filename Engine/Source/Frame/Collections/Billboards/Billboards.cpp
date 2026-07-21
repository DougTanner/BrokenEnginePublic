#include "Billboards.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<BillboardsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<BillboardsPostRender>;

void BillboardsInterpolate::Register()
{
}

void BillboardsInterpolate::AllocateAndCopy(BillboardsInterpolate& rCurrent, const BillboardsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void BillboardsPostRender::AllocateAndCopy(BillboardsPostRender& rCurrent, const BillboardsPostRender& rPrevious)
{
	AllocateAndCopyIds(rCurrent, rPrevious);
}

void BillboardsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void BillboardsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	// Owned objects are transferred by their parent
}

void BillboardsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine

#endif // BT_CLIENT
