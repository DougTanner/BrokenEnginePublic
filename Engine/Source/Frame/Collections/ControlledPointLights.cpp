#include "ControlledPointLights.h"

#include "Frame/Frame.h"
#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"

namespace engine
{

ControllerKeyframe ControllerKeyframe::Lerp(const ControllerKeyframe& rA, const ControllerKeyframe& rB, float fPercent)
{
	return
	{
		.fVisibleArea = std::lerp(rA.fVisibleArea, rB.fVisibleArea, fPercent),
		.fVisibleIntensity = std::lerp(rA.fVisibleIntensity, rB.fVisibleIntensity, fPercent),
		.fLightingArea = std::lerp(rA.fLightingArea, rB.fLightingArea, fPercent),
		.fLightingIntensity = std::lerp(rA.fLightingIntensity, rB.fLightingIntensity, fPercent),
		.fRotation = std::lerp(rA.fRotation, rB.fRotation, fPercent),
	};
}

// Interpolate between keyframes based on elapsed time
static ControllerKeyframe InterpolateKeyframes(const ControllerType& rController, float fElapsedTime)
{
	int64_t iKeyframeCount = rController.uiKeyframeCount;

	// Clamp to animation bounds
	if (fElapsedTime <= rController.pfTimes[0])
	{
		return rController.keyframes[0];
	}
	if (fElapsedTime >= rController.pfTimes[iKeyframeCount - 1])
	{
		return rController.keyframes[iKeyframeCount - 1];
	}

	// Find keyframe segment
	for (int64_t j = 1; j < iKeyframeCount; ++j)
	{
		if (fElapsedTime < rController.pfTimes[j])
		{
			float fPreviousTime = rController.pfTimes[j - 1];
			float fPercent = (fElapsedTime - fPreviousTime) / (rController.pfTimes[j] - fPreviousTime);
			return ControllerKeyframe::Lerp(rController.keyframes[j - 1], rController.keyframes[j], fPercent);
		}
	}

	return rController.keyframes[iKeyframeCount - 1];
}

void ControlledPointLightsInterpolate::Update([[maybe_unused]] ControlledPointLightsInterpolate& __restrict rCurrent, [[maybe_unused]] const ControlledPointLightsInterpolate& __restrict rPrevious, [[maybe_unused]] PointLightsInterpolate& __restrict rPointLights, [[maybe_unused]] float fCurrentTime)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load controller metadata
		uint8_t uiControllerTypeIndex = rPrevious.puiControllerTypeIndices[i];
		float fStartTime = rPrevious.pfStartTimes[i];
		float fBaseRotation = rPrevious.pfBaseRotations[i];
		point_lights_t pointLightId = rPrevious.pPointLightIds[i];

		// Interpolate based on elapsed time
		float fElapsedTime = fCurrentTime - fStartTime;
		const ControllerType& rController = sControllerTypes.at(uiControllerTypeIndex);
		ControllerKeyframe interpolated = InterpolateKeyframes(rController, fElapsedTime);

		// Save controller metadata
		rCurrent.puiControllerTypeIndices[i] = uiControllerTypeIndex;
		rCurrent.pfStartTimes[i] = fStartTime;
		rCurrent.pfBaseRotations[i] = fBaseRotation;
		rCurrent.pPointLightIds[i] = pointLightId;

		// Write interpolated values to the actual PointLight
		uint64_t uiPointLightIndex = rPointLights.IdToIndex(pointLightId);
		rPointLights.pfVisibleAreas[uiPointLightIndex] = interpolated.fVisibleArea;
		rPointLights.pfVisibleIntensities[uiPointLightIndex] = interpolated.fVisibleIntensity;
		rPointLights.pfLightingAreas[uiPointLightIndex] = interpolated.fLightingArea;
		rPointLights.pfLightingIntensities[uiPointLightIndex] = interpolated.fLightingIntensity;
		rPointLights.pfRotations[uiPointLightIndex] = fBaseRotation + interpolated.fRotation;
	}
}

void ControlledPointLightsInterpolate::Sync([[maybe_unused]] ControlledPointLightsInterpolate& __restrict rCurrent, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] float fDeltaTime)
{
}

uint8_t ControlledPointLightsPostRender::RegisterControllerType(const ControllerType& rType)
{
	ControlledPointLightsInterpolate::sControllerTypes.push_back(rType);
	return static_cast<uint8_t>(ControlledPointLightsInterpolate::sControllerTypes.size() - 1);
}

const ControllerType& ControlledPointLightsPostRender::GetControllerType(uint8_t uiIndex)
{
	return ControlledPointLightsInterpolate::sControllerTypes.at(uiIndex);
}

void ControlledPointLightsPostRender::Update([[maybe_unused]] ControlledPointLightsPostRender& __restrict rCurrent, [[maybe_unused]] const ControlledPointLightsPostRender& __restrict rPrevious)
{
	engine::ReallocateAndCopyMetadata(rCurrent, rPrevious, rCurrent.Members());

	if (rCurrent.pData == nullptr)
	{
		return;
	}

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		uint8_t uiFlags = rPrevious.puiFlags[i];

		// Save
		rCurrent.puiFlags[i] = uiFlags;
	}
}

void XM_CALLCONV ControlledPointLightsPostRender::Add(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fRotation)
{
	// Create the underlying PointLight
	const ControllerType& rController = ControlledPointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);
	point_lights_t pointLightId = PointLightsPostRender::Add(rFrame, rController.uiTypeIndex);

	// Set initial position on the PointLight
	uint64_t uiPointLightIndex = rFrame.interpolate.pointLights.IdToIndex(pointLightId);
	rFrame.interpolate.pointLights.pVecPositions[uiPointLightIndex] = vecPosition;

	// Set initial values from first keyframe
	rFrame.interpolate.pointLights.pfVisibleAreas[uiPointLightIndex] = rController.keyframes[0].fVisibleArea;
	rFrame.interpolate.pointLights.pfVisibleIntensities[uiPointLightIndex] = rController.keyframes[0].fVisibleIntensity;
	rFrame.interpolate.pointLights.pfLightingAreas[uiPointLightIndex] = rController.keyframes[0].fLightingArea;
	rFrame.interpolate.pointLights.pfLightingIntensities[uiPointLightIndex] = rController.keyframes[0].fLightingIntensity;
	rFrame.interpolate.pointLights.pfRotations[uiPointLightIndex] = fRotation + rController.keyframes[0].fRotation;

	// Create the controller entry
	ControlledPointLightsInterpolate& rInterpolate = rFrame.interpolate.controlledPointLights;
	ControlledPointLightsPostRender& rPostRender = rFrame.postRender.controlledPointLights;

	engine::GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iIndex = engine::AddElement(rInterpolate, rPostRender);

	rInterpolate.puiControllerTypeIndices[iIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[iIndex] = fCurrentTime;
	rInterpolate.pfBaseRotations[iIndex] = fRotation;
	rInterpolate.pPointLightIds[iIndex] = pointLightId;

	rPostRender.puiFlags[iIndex] = 0;
}

void ControlledPointLightsPostRender::Destroy(game::Frame& __restrict rFrame, float fCurrentTime)
{
	ControlledPointLightsInterpolate& rInterpolate = rFrame.interpolate.controlledPointLights;
	ControlledPointLightsPostRender& rPostRender = rFrame.postRender.controlledPointLights;

	for (int64_t i = 0; i < rInterpolate.iCount; ++i)
	{
		// Check for bDestroysSelf expiration
		uint8_t uiControllerTypeIndex = rInterpolate.puiControllerTypeIndices[i];
		float fStartTime = rInterpolate.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;

		const ControllerType& rController = ControlledPointLightsInterpolate::sControllerTypes.at(uiControllerTypeIndex);

		bool bExpired = fElapsedTime > rController.pfTimes[rController.uiKeyframeCount - 1];

		if (rController.bDestroysSelf && bExpired) [[unlikely]]
		{
			// Remove the underlying PointLight
			point_lights_t pointLightId = rInterpolate.pPointLightIds[i];
			PointLightsPostRender::Remove(rFrame, pointLightId);

			// Swap-and-pop controller
			if (rInterpolate.iCount - 1 > i) [[likely]]
			{
				engine::SwapElement(rInterpolate, i, rInterpolate.Members());
				engine::SwapElement(rPostRender, i, rPostRender.Members());
				--i;
			}
			--rInterpolate.iCount;
			--rPostRender.iCount;
		}
	}
}

bool ControlledPointLightsInterpolate::operator==(const ControlledPointLightsInterpolate& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiControllerTypeIndices[i], rOther.puiControllerTypeIndices[i]);
		bEqual &= common::BreakOnNotEqual(pfStartTimes[i], rOther.pfStartTimes[i]);
		bEqual &= common::BreakOnNotEqual(pfBaseRotations[i], rOther.pfBaseRotations[i]);
		bEqual &= common::BreakOnNotEqual(pPointLightIds[i], rOther.pPointLightIds[i]);
	}

	return bEqual;
}

bool ControlledPointLightsPostRender::operator==(const ControlledPointLightsPostRender& rOther) const
{
	bool bEqual = true;
	bEqual &= common::BreakOnNotEqual<Collection>(*this, rOther);

	for (int64_t i = 0; i < iCount; ++i)
	{
		bEqual &= common::BreakOnNotEqual(puiFlags[i], rOther.puiFlags[i]);
	}

	return bEqual;
}

} // namespace engine
