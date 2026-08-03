#include "PointLights.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<PointLightsPostRender>;

void PointLightsInterpolate::AllocateAndCopy(PointLightsInterpolate& rCurrent, const PointLightsInterpolate& rPrevious)
{
	AllocateAndCopyMembers(rCurrent, rPrevious);
}

void PointLightsPostRender::AllocateAndCopy(PointLightsPostRender& rCurrent, const PointLightsPostRender& rPrevious)
{
	engine::AllocateAndCopyMembers(rCurrent, rPrevious);
}

void PointLightsPostRender::Destroy(game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	DestroyExpiredControlled(rFrame.interpolate.pointLights, rFrame.postRender.pointLights, rFrame.interpolate.fCurrentTime,
		[](auto& rI, auto& rPR, int64_t& i)
		{
			RemoveIndexableElement(rI, rPR, rPR.puiIds[i], rI.Members(), rPR.Members());
			--i;
		});
}

} // namespace engine

#endif // BT_CLIENT
