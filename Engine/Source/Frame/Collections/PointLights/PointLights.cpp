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

void PointLightsPostRender::Spawn([[maybe_unused]] game::Frame& __restrict rFrame)
{
}

void PointLightsPostRender::Transfer([[maybe_unused]] game::Frame& __restrict rFrame)
{
	// Owned objects are transferred by their parent
}

void PointLightsPostRender::Destroy(game::Frame& __restrict rFrame)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	float fCurrentTime = rFrame.interpolate.fCurrentTime;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];

		// Skip non-controlled lights
		if (uiControllerTypeIndex == kuiInvalidControllerType)
		{
			continue;
		}

		const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

		// Skip if not auto-destroy
		if (!rController.bDestroysSelf)
		{
			continue;
		}

		// Check if animation has expired
		float fStartTime = rInterpolate.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;
		bool bExpired = fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1];

		if (bExpired) [[unlikely]]
		{
			// Remove the point light using swap-and-pop
			point_lights_t id = rPostRender.puiIds[i];
			engine::RemoveIndexableElement(rInterpolate, rPostRender, id, rInterpolate.Members(), rPostRender.Members());
			--i; // Re-check this index (new element swapped in)
		}
	}
}

} // namespace engine

#endif // BT_CLIENT
