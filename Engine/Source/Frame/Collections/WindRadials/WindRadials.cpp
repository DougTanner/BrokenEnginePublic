#include "WindRadials.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<WindRadialsInterpolate>;
template struct Collection<WindRadialsPostRender>;

void WindRadialsInterpolate::AllocateAndCopy(WindRadialsInterpolate& rCurrent, const WindRadialsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void WindRadialsPostRender::AllocateAndCopy(WindRadialsPostRender& rCurrent, const WindRadialsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

void WindRadialsPostRender::Destroy(game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	DestroyExpiredControlled(rFrame.interpolate.windRadials, rFrame.postRender.windRadials, rFrame.interpolate.fCurrentTime,
		[](auto& rI, auto& rPR, int64_t& i)
		{
			DestroyElement(rI, rPR, i, rI.Members(), rPR.Members());
		});
}

} // namespace engine

#endif // BT_CLIENT
