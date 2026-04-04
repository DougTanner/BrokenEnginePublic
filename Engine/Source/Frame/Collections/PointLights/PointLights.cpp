#include "PointLights.h"

#if defined(BT_CLIENT)

namespace engine
{

template struct Collection<PointLightsInterpolate, CollectionFlags::kIdToIndex>;
template struct Collection<PointLightsPostRender>;

void PointLightsInterpolate::Register()
{
}

void PointLightsInterpolate::AllocateAndCopy(PointLightsInterpolate& rCurrent, const PointLightsInterpolate& rPrevious)
{
	Allocate(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.iCount > 0)
	{
		std::memcpy(rCurrent.puiTypeIndices, rPrevious.puiTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiTypeIndices[0]));
		std::memcpy(rCurrent.puiControllerTypeIndices, rPrevious.puiControllerTypeIndices, rCurrent.iCount * sizeof(rCurrent.puiControllerTypeIndices[0]));
		std::memcpy(rCurrent.pfStartTimes, rPrevious.pfStartTimes, rCurrent.iCount * sizeof(rCurrent.pfStartTimes[0]));
		std::memcpy(rCurrent.pfBaseRotations, rPrevious.pfBaseRotations, rCurrent.iCount * sizeof(rCurrent.pfBaseRotations[0]));
	}
}

void PointLightsPostRender::AllocateAndCopy(PointLightsPostRender& rCurrent, const PointLightsPostRender& rPrevious)
{
	AllocateAndCopyIds(rCurrent, rPrevious);
}

void PointLightsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void PointLightsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
	// Owned objects are transferred by their parent
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
