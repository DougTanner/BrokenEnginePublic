#include "Sounds.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<SoundsPostRender>;

void SoundsInterpolate::AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void SoundsPostRender::AllocateAndCopy(SoundsPostRender& rCurrent, const SoundsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

} // namespace engine

#endif // BT_CLIENT
