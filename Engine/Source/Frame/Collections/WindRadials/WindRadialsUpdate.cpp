#include "WindRadials.h"

#if defined(BT_CLIENT)

#include "Ui/GraphicsSettingsWrappersBase.h"
#include "Ui/WrapperBase.h"

namespace engine
{

void WindRadialsInterpolate::Update([[maybe_unused]] game::FrameInterpolate& __restrict rFrameInterpolate, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame)
{
	if (!gWindEnabled.Get<bool>())
	{
		return;
	}

	WindRadialsInterpolate& __restrict rCurrent = rFrameInterpolate.windRadials;
	const WindRadialsInterpolate& rPrevious = rPreviousFrame.interpolate.windRadials;
	float fCurrentTime = rPreviousFrame.interpolate.fCurrentTime + rFrameInterpolate.fDeltaTime;

	for (int64_t i = 0; i < rCurrent.iCount; ++i)
	{
		// Load from previous frame
		XMVECTOR vecPosition = rPrevious.pVecPositions[i];
		float fBaseIntensity = rCurrent.pfBaseIntensities[i];
		float fBaseSize = rCurrent.pfBaseSizes[i];

		// Interpolate controller keyframes
		uint8_t uiControllerTypeIndex = rCurrent.puiControllerTypeIndices[i];
		float fStartTime = rCurrent.pfStartTimes[i];
		float fElapsedTime = fCurrentTime - fStartTime;
		const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);
		WindRadialKeyframe interpolated = InterpolateKeyframes(rController, fElapsedTime);

		// Save
		rCurrent.pVecPositions[i] = vecPosition;
		rCurrent.pfIntensities[i] = fBaseIntensity * interpolated.fIntensity;
		rCurrent.pfSizes[i] = fBaseSize * interpolated.fSize;
	}
}

void WindRadialsPostRender::Update([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void XM_CALLCONV WindRadialsPostRender::AddControlled(game::Frame& __restrict rFrame, float fCurrentTime, uint8_t uiControllerTypeIndex, FXMVECTOR vecPosition, float fBaseIntensity, float fBaseSize)
{
	WindRadialsInterpolate& rInterpolate = rFrame.interpolate.windRadials;
	WindRadialsPostRender& rPostRender = rFrame.postRender.windRadials;

	// Get controller type
	const WindRadialControllerType& rController = WindRadialsInterpolate::GetControllerType(uiControllerTypeIndex);

	GrowPairedCollections(rInterpolate, rPostRender, rInterpolate.Members(), rPostRender.Members());
	int64_t iSpawnIndex = AddElement(rInterpolate, rPostRender);

	// Set position
	rInterpolate.pVecPositions[iSpawnIndex] = XMVectorSetW(vecPosition, 1.0f);

	// Initialize from base values * first keyframe
	rInterpolate.pfIntensities[iSpawnIndex] = fBaseIntensity * rController.keyframes[0].fIntensity;
	rInterpolate.pfSizes[iSpawnIndex] = fBaseSize * rController.keyframes[0].fSize;

	// Set controller fields
	rInterpolate.puiControllerTypeIndices[iSpawnIndex] = uiControllerTypeIndex;
	rInterpolate.pfStartTimes[iSpawnIndex] = fCurrentTime;
	rInterpolate.pfBaseIntensities[iSpawnIndex] = fBaseIntensity;
	rInterpolate.pfBaseSizes[iSpawnIndex] = fBaseSize;
}

void WindRadialsPostRender::PreCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void WindRadialsPostRender::PostCollision([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

void WindRadialsPostRender::AreaDamage([[maybe_unused]] game::Frame& __restrict rFrame, [[maybe_unused]] const game::Frame& __restrict rPreviousFrame, [[maybe_unused]] const FrameStaticData& rStaticData)
{
}

} // namespace engine

#endif // BT_CLIENT
