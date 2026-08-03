#include "Puffs.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<PuffsInterpolate>;
template struct Collection<PuffsPostRender>;

void PuffsInterpolate::AllocateAndCopy(PuffsInterpolate& rCurrent, const PuffsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void PuffsPostRender::AllocateAndCopy(PuffsPostRender& rCurrent, const PuffsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

void PuffsPostRender::Destroy(game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	DestroyExpiredControlled(rFrame.interpolate.puffs, rFrame.postRender.puffs, rFrame.interpolate.fCurrentTime,
		[](auto& rI, auto& rPR, int64_t& i)
		{
			DestroyElement(rI, rPR, i, rI.Members(), rPR.Members());
		});
}

} // namespace engine

#endif // BT_CLIENT
