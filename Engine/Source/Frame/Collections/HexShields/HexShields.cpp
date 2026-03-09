#include "HexShields.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<HexShieldsPostRender>;

void HexShieldsInterpolate::Register()
{
}

void HexShieldsInterpolate::AllocateAndCopy(HexShieldsInterpolate& rCurrent, const HexShieldsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.pfLightingIntensities, rPrevious.pfLightingIntensities, rCurrent.iCount * sizeof(rCurrent.pfLightingIntensities[0]));
		std::memcpy(rCurrent.pfSizes, rPrevious.pfSizes, rCurrent.iCount * sizeof(rCurrent.pfSizes[0]));
		std::memcpy(rCurrent.pfColorMixes, rPrevious.pfColorMixes, rCurrent.iCount * sizeof(rCurrent.pfColorMixes[0]));
	}
}

void HexShieldsPostRender::AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void HexShieldsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void HexShieldsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void HexShieldsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool HexShieldsInterpolate::operator==(const HexShieldsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		for (int64_t j = 0; j < 3; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pf4Transforms[j][i], rOther.pf4Transforms[j][i]);
			bEqual &= common::BreakOnNotEqual(pf4TransformNormals[j][i], rOther.pf4TransformNormals[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
		for (int64_t j = 0; j < shaders::kiHexShieldDirections; ++j)
		{
			bEqual &= common::BreakOnNotEqual(pf4Directions[j][i], rOther.pf4Directions[j][i]);
			bEqual &= common::BreakOnNotEqual(pfVertIntensities[j][i], rOther.pfVertIntensities[j][i]);
			bEqual &= common::BreakOnNotEqual(pfFragIntensities[j][i], rOther.pfFragIntensities[j][i]);
		}
		bEqual &= common::BreakOnNotEqual(pfLightingIntensities[i], rOther.pfLightingIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfSizes[i], rOther.pfSizes[i]);
		bEqual &= common::BreakOnNotEqual(pfColorMixes[i], rOther.pfColorMixes[i]);
	}

	return bEqual;
}

bool HexShieldsPostRender::operator==(const HexShieldsPostRender& rOther) const
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
