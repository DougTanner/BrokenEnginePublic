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

	uint32_t uiWindWidth = gpTextureManager->mRenderTargetTextures.mWindTextureOne.mInfo.extent.width;
	rGlobalLayout.uiWindTilesX = (uiWindWidth + 7) / 8;

	// Toggle ping-pong index
	giWindTextureIndex = 1 - giWindTextureIndex;

	rGlobalLayout.fWindTextureIndex = static_cast<float>(giWindTextureIndex);

	// Compute wind spread quad offset (shares smoke area coordinate space)
	static XMFLOAT4 sf4PreviousWindArea {};
	if (gbWindClear)
	{
		gbWindClear = false;
		sf4PreviousWindArea = {};
	}
	float fXOffset = (sf4PreviousWindArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousWindArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousWindArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousWindArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mWindSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Params = {};
	sf4PreviousWindArea = rGlobalLayout.f4SmokeArea;
}

} // namespace engine

#endif // BT_CLIENT
