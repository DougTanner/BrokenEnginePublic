#include "Billboards.h"

#ifdef BT_CLIENT

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

bool BillboardsInterpolate::operator==(const BillboardsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(puiFlags[i], rOther.puiFlags[i]);
		bEqual &= common::BreakOnNotEqual(pfRotations[i], rOther.pfRotations[i]);
		bEqual &= common::BreakOnNotEqual(pfExtra[i], rOther.pfExtra[i]);
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
	}

	return bEqual;
}

bool BillboardsPostRender::operator==(const BillboardsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

} // namespace engine

#endif // BT_CLIENT
