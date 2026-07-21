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
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void HexShieldsPostRender::AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious)
{
	AllocateAndCopyIds(rCurrent, rPrevious);
}

void HexShieldsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void HexShieldsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	// Owned objects are transferred by their parent
}

void HexShieldsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine

#endif // BT_CLIENT
