#include "Sounds.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<SoundsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<SoundsPostRender>;

void SoundsInterpolate::Register()
{
}

void SoundsInterpolate::AllocateAndCopy(SoundsInterpolate& rCurrent, const SoundsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void SoundsPostRender::AllocateAndCopy(SoundsPostRender& rCurrent, const SoundsPostRender& rPrevious)
{
	AllocateAndCopyIds(rCurrent, rPrevious);
}

void SoundsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	// Owned objects are transferred by their parent
}

void SoundsPostRender::Destroy([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void SoundsInterpolate::Render([[maybe_unused]] const game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] int64_t iCommandBuffer)
{
}

} // namespace engine

#endif // BT_CLIENT
