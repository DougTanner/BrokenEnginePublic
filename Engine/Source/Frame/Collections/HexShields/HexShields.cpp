#include "HexShields.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<HexShieldsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<HexShieldsPostRender>;

void HexShieldsInterpolate::AllocateAndCopy(HexShieldsInterpolate& rCurrent, const HexShieldsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void HexShieldsPostRender::AllocateAndCopy(HexShieldsPostRender& rCurrent, const HexShieldsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
