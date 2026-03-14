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
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.puiFlags, rPrevious.puiFlags, rCurrent.iCount * sizeof(rCurrent.puiFlags[0]));
		std::memcpy(rCurrent.pfRotations, rPrevious.pfRotations, rCurrent.iCount * sizeof(rCurrent.pfRotations[0]));
		std::memcpy(rCurrent.pfExtra, rPrevious.pfExtra, rCurrent.iCount * sizeof(rCurrent.pfExtra[0]));
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
	}
}

void BillboardsPostRender::AllocateAndCopy(BillboardsPostRender& rCurrent, const BillboardsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void BillboardsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void BillboardsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void BillboardsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
