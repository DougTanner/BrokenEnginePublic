#include "PointLights.h"

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

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

			ControllerKeyframe interpolated = InterpolateScaledKeyframes(rController, fElapsedTime, [](ControllerType& rScaledController, const ControllerType& rOriginalController, int64_t j)
			{
				if (rOriginalController.ppVisibleAreaScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fVisibleArea *= rOriginalController.ppVisibleAreaScales[j]->Get();
				}
				if (rOriginalController.ppVisibleIntensityScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fVisibleIntensity *= rOriginalController.ppVisibleIntensityScales[j]->Get();
				}
				if (rOriginalController.ppLightingAreaScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fLightingArea *= rOriginalController.ppLightingAreaScales[j]->Get();
				}
				if (rOriginalController.ppLightingIntensityScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fLightingIntensity *= rOriginalController.ppLightingIntensityScales[j]->Get();
				}
			});

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

void PointLightsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
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

	ZeroMemberRow(uiSpawnIndex, rInterpolate.Members());
	rInterpolate.pVecPositions[uiSpawnIndex] = XMVectorSetW(XMVectorZero(), 1.0f);
	rInterpolate.puiTypeIndices[uiSpawnIndex] = uiTypeIndex;
	rInterpolate.puiControllerTypeIndices[uiSpawnIndex] = kuiInvalidControllerType;
}

void PointLightsPostRender::Remove(game::Frame& __restrict rFrame, point_lights_t& rId)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	RemoveIndexableElementAndClearHandle(rInterpolate, rPostRender, rId, rInterpolate.Members(), rPostRender.Members());
}

void XM_CALLCONV PointLightsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation)
{
	PointLightsInterpolate& rInterpolate = rFrame.interpolate.pointLights;
	PointLightsPostRender& rPostRender = rFrame.postRender.pointLights;

	// Get controller type and base type
	const ControllerType& rController = PointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

	AddControlledElement(rInterpolate, rPostRender, fCurrentTime, uiControllerTypeIndex, vecPosition,
		[&rInterpolate, &rPostRender]()
		{
			GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
		},
		[&rInterpolate, &rPostRender, &rFrame]()
		{
			auto [uiSpawnIndex, newId] = AddVisualIndexableElement(rInterpolate, rPostRender, rFrame.postRender);
			rPostRender.puiIds[uiSpawnIndex] = newId;
			return uiSpawnIndex;
		},
		[&rInterpolate, &rController, fRotation](int64_t iSpawnIndex)
		{
			rInterpolate.puiTypeIndices[iSpawnIndex] = rController.uiBaseTypeIndex;
			rInterpolate.pfVisibleAreas[iSpawnIndex] = rController.keyframes[0].fVisibleArea * (rController.ppVisibleAreaScales[0] != nullptr ? rController.ppVisibleAreaScales[0]->Get() : 1.0f);
			rInterpolate.pfVisibleIntensities[iSpawnIndex] = rController.keyframes[0].fVisibleIntensity * (rController.ppVisibleIntensityScales[0] != nullptr ? rController.ppVisibleIntensityScales[0]->Get() : 1.0f);
			rInterpolate.pfLightingAreas[iSpawnIndex] = rController.keyframes[0].fLightingArea * (rController.ppLightingAreaScales[0] != nullptr ? rController.ppLightingAreaScales[0]->Get() : 1.0f);
			rInterpolate.pfLightingIntensities[iSpawnIndex] = rController.keyframes[0].fLightingIntensity * (rController.ppLightingIntensityScales[0] != nullptr ? rController.ppLightingIntensityScales[0]->Get() : 1.0f);
			rInterpolate.pfRotations[iSpawnIndex] = fRotation + rController.keyframes[0].fRotation;
			rInterpolate.pfBaseRotations[iSpawnIndex] = fRotation;
		});
}

} // namespace engine

#endif // BT_CLIENT
