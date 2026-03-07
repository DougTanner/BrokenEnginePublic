#include "Targets.h"

namespace engine
{
template struct Collection<game::TargetsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<game::TargetsPostRender>;
}

namespace game
{

void TargetsInterpolate::Register()
{
}

void TargetsInterpolate::AllocateAndCopy(TargetsInterpolate& rCurrent, const TargetsInterpolate& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.pVecPositions, rPrevious.pVecPositions, rCurrent.iCount * sizeof(rCurrent.pVecPositions[0]));
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
	}
}

void TargetsPostRender::AllocateAndCopy(TargetsPostRender& rCurrent, const TargetsPostRender& rPrevious)
{
	engine::Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
		std::memcpy(rCurrent.pFlags, rPrevious.pFlags, rCurrent.iCount * sizeof(rCurrent.pFlags[0]));
		std::memcpy(rCurrent.puiSubscribers, rPrevious.puiSubscribers, rCurrent.iCount * sizeof(rCurrent.puiSubscribers[0]));
		std::memcpy(rCurrent.pAlignments, rPrevious.pAlignments, rCurrent.iCount * sizeof(rCurrent.pAlignments[0]));
	}
}

void TargetsPostRender::Transfer([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Destroy([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::Spawn([[maybe_unused]] Frame& __restrict rFrame)
{
}

void TargetsPostRender::RegisterType(uint8_t& ruiIndex, const TargetsType& rType)
{
	ASSERT(ruiIndex == 0xFF);
	ruiIndex = static_cast<uint8_t>(TargetsInterpolate::sTypes.size());
	TargetsInterpolate::sTypes.push_back(rType);
}

bool TargetsInterpolate::operator==(const TargetsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
		bEqual &= common::BreakOnNotEqual(puiTypeIndices[i], rOther.puiTypeIndices[i]);
	}

	return bEqual;
}

bool TargetsPostRender::operator==(const TargetsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
		bEqual &= common::BreakOnNotEqual(puiSubscribers[i], rOther.puiSubscribers[i]);
		bEqual &= common::BreakOnNotEqual(pAlignments[i], rOther.pAlignments[i]);
	}

	return bEqual;
}

bool TargetsInterpolate::ServerCompare(const TargetsInterpolate& rOther) const { return *this == rOther; }

bool TargetsPostRender::ServerCompare(const TargetsPostRender& rOther) const { return *this == rOther; }

void TargetsInterpolate::Render([[maybe_unused]] const FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace game
