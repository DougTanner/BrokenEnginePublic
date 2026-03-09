#include "PointLights.h"

#if defined(BT_CLIENT)

namespace engine
{

void PointLightsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PointLightsInterpolate& __restrict rCurrent = rFrameInterpolate.pointLights;
	const PointLightsInterpolate& rPrevious = rPreviousFrame.interpolate.pointLights;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	if (rCurrent.iCount == 0)
	{
		return;
	}

	// Note: Owner is responsible for writing position each frame via IdToIndex

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fRotation = rPrevious.pfRotations[i];
		float fVisibleArea = rPrevious.pfVisibleAreas[i];
		float fVisibleIntensity = rPrevious.pfVisibleIntensities[i];
		float fLightingArea = rPrevious.pfLightingAreas[i];
		float fLightingIntensity = rPrevious.pfLightingIntensities[i];

		// Load controller fields (copied in AllocateAndCopy)
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];
		float fBaseRotation = rCurrent.pfBaseRotations[i];

		// Apply controller interpolation if this is a controlled light
		if (uiControllerTypeIndex != kuiInvalidControllerType)
		{
			float fElapsedTime = fCurrentTime - fStartTime;
			const ControllerType& rController = sControllerTypes.at(uiControllerTypeIndex);
			ControllerKeyframe interpolated = InterpolateKeyframes(rController, fElapsedTime);

			fVisibleArea = interpolated.fVisibleArea;
			fVisibleIntensity = interpolated.fVisibleIntensity;
			fLightingArea = interpolated.fLightingArea;
			fLightingIntensity = interpolated.fLightingIntensity;
			fRotation = fBaseRotation + interpolated.fRotation;
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfRotations[i] = fRotation;
		rCurrent.pfVisibleAreas[i] = fVisibleArea;
		rCurrent.pfVisibleIntensities[i] = fVisibleIntensity;
		rCurrent.pfLightingAreas[i] = fLightingArea;
		rCurrent.pfLightingIntensities[i] = fLightingIntensity;
	}
}

void PointLightsInterpolate::Sync(game::FrameInterpolate& rFrameInterpolate, id_t id, const SyncData& rData)
{
	PointLightsInterpolate& rPointLights = rFrameInterpolate.pointLights;
	int64_t iIndex = rPointLights.IdToIndex(id);

	rPointLights.pVecPositions[iIndex] = XMVectorSetW(rData.vecPosition, 1.0f);
	rPointLights.pfVisibleAreas[iIndex] = rData.fVisibleArea;
	rPointLights.pfVisibleIntensities[iIndex] = rData.fVisibleIntensity;
	rPointLights.pfLightingAreas[iIndex] = rData.fLightingArea;
	rPointLights.pfLightingIntensities[iIndex] = rData.fLightingIntensity;
	rPointLights.pfRotations[iIndex] = rData.fRotation;
}

void PointLightsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::Add(game::Frame& __restrict rFrame, point_lights_t& rId, uint8_t uiTypeIndex)
{
	ASSERT(!rId.IsValid());

	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rId = newId;
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Zero-init all members
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorSetW(XMVectorZero(), 1.0f);
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.pfRotations[uiSpawnIndex] = 0.0f;
	rInterpolate.pfVisibleAreas[uiSpawnIndex] = 0.0f;
	rInterpolate.pfVisibleIntensities[uiSpawnIndex] = 0.0f;
	rInterpolate.pfLightingAreas[uiSpawnIndex] = 0.0f;
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = 0.0f;

	// Controller fields: not controlled
	rInterpolate.puiControllerTypeIndices[uiSpawnIndex] = kuiInvalidControllerType;
	rInterpolate.pfStartTimes[uiSpawnIndex] = 0.0f;
	rInterpolate.pfBaseRotations[uiSpawnIndex] = 0.0f;
}

void PointLightsPostRender::Remove(game::Frame& __restrict rFrame, point_lights_t& rId)
{
	ASSERT(rId.IsValid());

	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	RemoveIndexableElement(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());

	rId = {};
}

void XM_CALLCONV PointLightsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	// Get controller type and base type
	const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
	rPostRender.puiIds[uiSpawnIndex] = newId;

	// Set position and base type from controller
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorSetW(vecPosition, 1.0f);
	rInterpolate.puiTypeIndices[uiSpawnIndex] = rController.uiBaseTypeIndex;

	// Initialize per-instance values from first keyframe
	rInterpolate.pfVisibleAreas[uiSpawnIndex] = rController.keyframes[0].fVisibleArea;
	rInterpolate.pfVisibleIntensities[uiSpawnIndex] = rController.keyframes[0].fVisibleIntensity;
	rInterpolate.pfLightingAreas[uiSpawnIndex] = rController.keyframes[0].fLightingArea;
	rInterpolate.pfLightingIntensities[uiSpawnIndex] = rController.keyframes[0].fLightingIntensity;
	rInterpolate.pfRotations[uiSpawnIndex] = fRotation + rController.keyframes[0].fRotation;

	// Set controller fields
	rInterpolate.puiControllerTypeIndices[uiSpawnIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[uiSpawnIndex] = fCurrentTime;
	rInterpolate.pfBaseRotations[uiSpawnIndex] = fRotation;
}

void PointLightsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

void PointLightsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
}

} // namespace engine

#endif // BT_CLIENT
