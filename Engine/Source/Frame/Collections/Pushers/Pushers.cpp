#include "Pushers.h"

#include "Profile/ProfileManager.h"

namespace engine
{

template struct Collection<PushersInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<PushersPostRender>;

void PushersInterpolate::Register()
{
}

void PushersInterpolate::AllocateAndCopy(PushersInterpolate& rCurrent, const PushersInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());
}

void PushersPostRender::AllocateAndCopy(PushersPostRender& rCurrent, const PushersPostRender& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiIds, rPrevious.puiIds, rCurrent.iCount * sizeof(rCurrent.puiIds[0]));
	}
}

void PushersPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PushersPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void PushersPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

bool PushersInterpolate::operator==(const PushersInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	if (iCount != rOther.iCount)
		FILE_LOG(0, "[ServerCompare] PushersInterpolate count: client={} server={}", iCount, rOther.iCount);

	for (int64_t i = 0; i < iCount; ++i)
	{
		{
			bool bPos = common::BreakOnNotEqual(pVecPositions[i], rOther.pVecPositions[i]);
			if (!bPos) FILE_LOG(0, "[ServerCompare] PushersInterpolate pos: i={}/{} client=({:.6f},{:.6f},{:.6f}) server=({:.6f},{:.6f},{:.6f})", i, iCount, XMVectorGetX(pVecPositions[i]), XMVectorGetY(pVecPositions[i]), XMVectorGetZ(pVecPositions[i]), XMVectorGetX(rOther.pVecPositions[i]), XMVectorGetY(rOther.pVecPositions[i]), XMVectorGetZ(rOther.pVecPositions[i]));
			bEqual &= bPos;
		}
		bEqual &= common::BreakOnNotEqual(pfRadii[i], rOther.pfRadii[i]);
		bEqual &= common::BreakOnNotEqual(pfIntensities[i], rOther.pfIntensities[i]);
		bEqual &= common::BreakOnNotEqual(pfPowers[i], rOther.pfPowers[i]);
		bEqual &= common::BreakOnNotEqual(pFlags[i], rOther.pFlags[i]);
	}

	return bEqual;
}

bool PushersPostRender::operator==(const PushersPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiIds[i], rOther.puiIds[i]);
	}

	return bEqual;
}

bool PushersInterpolate::ServerCompare(const PushersInterpolate& rOther) const { return *this == rOther; }

bool PushersPostRender::ServerCompare(const PushersPostRender& rOther) const { return *this == rOther; }

static int64_t siTotalCount = 0;

void PushersInterpolate::BeginRender([[maybe_unused]] int64_t iCommandBuffer, [[maybe_unused]] const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, [[maybe_unused]] const std::vector<GridCoord>& rActiveCoords)
{
	siTotalCount = 0;
}

void PushersInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
	siTotalCount += rFrameInterpolate.pushers.iCount;
}

void PushersInterpolate::EndRender([[maybe_unused]] int64_t iCommandBuffer)
{
	gpProfileManager->SetCount(kCpuCounterPushers, siTotalCount);
}

} // namespace engine
