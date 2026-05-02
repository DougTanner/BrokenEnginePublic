#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

void RenderWindGlobal(int64_t iCommandBuffer)
{
	// Accumulate wind time (always tick to avoid delta spikes after toggle)
	static common::Timer sWindTimer;
	static float sfWindTime = 0.0f;
	float fDeltaTime = common::NanosecondsToFloatSeconds<float>(sWindTimer.GetDeltaNs(true));
	sfWindTime += fDeltaTime * gWindTimeScale.Get();

	// Handle wind enable/disable toggle
	static bool sbWind = false;
	if (sbWind != gWind.Get<bool>())
	{
		sbWind = gWind.Get<bool>();
		gbWindClear = true;
	}

	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	// Set wind uniforms
	rGlobalLayout.fWindAdvectionScaleHigh = gWindAdvectionScaleHigh.Get();
	rGlobalLayout.fWindAdvectionScaleLow = gWindAdvectionScaleLow.Get();
	rGlobalLayout.fWindSwirlScaleHigh = gWindSwirlScaleHigh.Get();
	rGlobalLayout.fWindSwirlScaleLow = gWindSwirlScaleLow.Get();
	rGlobalLayout.fWindSwirlAmountHigh = gWindSwirlAmountHigh.Get();
	rGlobalLayout.fWindSwirlAmountLow = gWindSwirlAmountLow.Get();
	rGlobalLayout.fWindSwirlSpeedHigh = gWindSwirlSpeedHigh.Get();
	rGlobalLayout.fWindSwirlSpeedLow = gWindSwirlSpeedLow.Get();
	rGlobalLayout.fWindVorticityConfinementHigh = gWindVorticityConfinementHigh.Get();
	rGlobalLayout.fWindVorticityConfinementLow = gWindVorticityConfinementLow.Get();
	rGlobalLayout.fWindDecayHigh = gWindDecayHigh.Get();
	rGlobalLayout.fWindDecayLow = gWindDecayLow.Get();
	rGlobalLayout.fWindMomentumHigh = gWindMomentumHigh.Get();
	rGlobalLayout.fWindMomentumLow = gWindMomentumLow.Get();
	rGlobalLayout.fWindThresholdLow = gWindThresholdLow.Get();
	rGlobalLayout.fWindThresholdHigh = gWindThresholdHigh.Get();
	rGlobalLayout.fWindToSmokeStrength = gWindToSmokeStrength.Get();
	rGlobalLayout.fWindTimeScale = fDeltaTime * 60.0f * gWindTimeScale.Get();
	rGlobalLayout.fWindTexelSize = 1.0f / static_cast<float>(gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width);
	rGlobalLayout.fWindTime = sfWindTime;
	rGlobalLayout.fWindSmokeRetention = gWindSmokeRetention.Get();
	rGlobalLayout.fWindToSmokePower = gWindToSmokePower.Get();
	rGlobalLayout.fWindDiffusionHigh = gWindDiffusionHigh.Get();
	rGlobalLayout.fWindDiffusionLow = gWindDiffusionLow.Get();

	rGlobalLayout.fWindDisplacementNoiseScale = gWindDisplacementNoiseScale.Get();
	rGlobalLayout.fWindSmokeAdvection = gWindSmokeAdvection.Get();

	uint32_t uiWindWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	uint32_t uiWindHeight = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.height;
	rGlobalLayout.uiWindTilesX = (uiWindWidth + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;
	rGlobalLayout.uiWindTilesY = (uiWindHeight + shaders::kiComputeTileSize - 1) / shaders::kiComputeTileSize;

	// Toggle ping-pong index
	giWindTextureIndex = 1 - giWindTextureIndex;

	rGlobalLayout.fWindTextureIndex = static_cast<float>(giWindTextureIndex);

	// Wind shares smoke's f4SmokeArea / f4PreviousSmokeArea (already populated by
	// RenderSmokeGlobal earlier in the frame per the global-pass ordering contract).
	// Wind clear relies on the decay/soft-clamp path inside WindSpread() to zero
	// stale content over a few frames — same behavior as before the WriteSpreadQuad
	// removal (the previous out-of-bounds sampler trick was equally weak).
	if (gbWindClear)
	{
		gbWindClear = false;
	}
}

} // namespace engine

#endif // defined(BT_CLIENT)
