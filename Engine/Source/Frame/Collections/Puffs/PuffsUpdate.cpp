#include "Puffs.h"

#if defined(BT_CLIENT)

#include "Ui/WrapperBase.h"

namespace engine
{

void PuffsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	PuffsInterpolate& __restrict rCurrent = rFrameInterpolate.puffs;
	const PuffsInterpolate& rPrevious = rPreviousFrame.interpolate.puffs;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fIntensity = rPrevious.pfIntensities[i];
		float fArea = rPrevious.pfAreas[i];
		float fRotation = rPrevious.pfRotations[i];

		// Load controller fields (copied in AllocateAndCopy)
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];

		// Apply controller interpolation if this is a controlled puff
		if (uiControllerTypeIndex != kuiInvalidControllerType)
		{
			float fElapsedTime = fCurrentTime - fStartTime;
			const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);

			PuffKeyframe interpolated = InterpolateScaledKeyframes(rController, fElapsedTime, [](PuffControllerType& rScaledController, const PuffControllerType& rOriginalController, int64_t j)
			{
				if (rOriginalController.ppAreaScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fArea *= rOriginalController.ppAreaScales[j]->Get();
				}
				if (rOriginalController.ppIntensityScales[j] != nullptr)
				{
					rScaledController.keyframes[j].fIntensity *= rOriginalController.ppIntensityScales[j]->Get();
				}
			});

			// Map PuffKeyframe fields to puff properties
			fArea = interpolated.fArea;
			fIntensity = interpolated.fIntensity;
			fRotation = interpolated.fRotation;
		}

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fIntensity;
		rCurrent.pfAreas[i] = fArea;
		rCurrent.pfRotations[i] = fRotation;
	}
}

void PuffsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void XM_CALLCONV PuffsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition)
{
	PuffsInterpolate& rInterpolate = rFrame.interpolate.puffs;
	PuffsPostRender& rPostRender = rFrame.postRender.puffs;

	// Get controller type
	const PuffControllerType& rController = PuffsInterpolate::GetControllerType(uiControllerTypeIndex);

	AddControlledElement(rInterpolate, rPostRender, fCurrentTime, uiControllerTypeIndex, vecPosition,
		[&rInterpolate, &rPostRender]()
		{
			GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
		},
		[&rInterpolate, &rPostRender]()
		{
			return AddElement(rInterpolate, rPostRender);
		},
		[&rInterpolate, &rController](int64_t iSpawnIndex)
		{
			rInterpolate.puiTypeIndices[iSpawnIndex] = rController.uiBaseTypeIndex;
			rInterpolate.pfAreas[iSpawnIndex] = rController.keyframes[0].fArea * (rController.ppAreaScales[0] != nullptr ? rController.ppAreaScales[0]->Get() : 1.0f);
			rInterpolate.pfIntensities[iSpawnIndex] = rController.keyframes[0].fIntensity * (rController.ppIntensityScales[0] != nullptr ? rController.ppIntensityScales[0]->Get() : 1.0f);
			rInterpolate.pfRotations[iSpawnIndex] = rController.keyframes[0].fRotation;
		});
}

} // namespace engine

#endif // BT_CLIENT
