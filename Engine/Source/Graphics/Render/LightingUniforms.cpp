#if defined(BT_CLIENT)

#include "Render.h"

#include "Graphics/Camera.h"
#include "Ui/HeightLerpWrapperQuartet.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/PbrWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

static void PopulateLightingParameters(shaders::GlobalLayout& rGlobalLayout)
{
	// Lighting area: world-sized ramped texels in a pre-sized texture (mirror of the shadow-area path in
	// PopulateShadowParameters). The deposit/spread/combine textures are pre-sized (RenderTargetTextures, via
	// LightingDetailTextureSize) by kfLightingHeadroomMultiplier; the texel world size is sized so a constant
	// on-screen pixel count (textureWidth / kfLightingHeadroomMultiplier) spans the live frustum width at the camera's
	// rate-limited mfLightingTexelEyeHeight, so it is fixed at a settled eye height (the grid snaps cleanly under XY pan
	// -> no shimmer) and only rescales while the ramp tracks a zoom. Reading the actual (clamped) extent keeps coverage
	// device-clamp-invariant and snaps deposit quads onto integer texels; the headroom multiplier cancels out of the
	// window count. f4LightingArea is the full camera-centered footprint snapped to the deposit texel grid. Snapping to
	// the deposit grid (not combine) is load-bearing: deposit is where lights rasterize, so its grid must move in
	// integer-texel steps under pan. Spread/combine/temporal resample the same world rectangle at their own resolutions.
	float fLightingTextureWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width);
	float fLightingTextureHeight = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height);

	WorldSizedTexelArea area = ComputeWorldSizedTexelArea(game::Camera::kfLightingHeadroomMultiplier, game::gpCamera->mfLightingTexelEyeHeight, fLightingTextureWidth, fLightingTextureHeight, gpSwapchainManager->mfAspectRatio, gFov.Get(), game::gpCamera->mVecPosition);
	rGlobalLayout.f4LightingArea = area.f4Area;

	// Temporal accumulation: feed the previous frame's lighting area so LightingTemporal.comp can reproject the
	// history into the current grid (mirror of the shadow previous-area latch). First frame: previous == current and
	// blend forced to 1.0 (pure current) so the uninitialized history textures are never shown; that frame's copy
	// seeds valid history. Once-per-frame latch (RenderFrameGlobal runs once per frame).
	// A Graphics recreate (device-lost / settings) rebuilt the lighting history textures with undefined contents while
	// this static survived. The reset re-arms the first-frame guard so this frame blends pure-current and re-seeds history.
	static TemporalAreaLatch sTemporalAreaLatch {};
	rGlobalLayout.fLightingTemporalBlend = sTemporalAreaLatch.Update(rGlobalLayout.f4LightingArea, gbLightingTemporalReset, gLightingTemporalBlend.Get(), rGlobalLayout.f4LightingAreaPrevious);

	// Edge-fade denominator, deliberately floored unlike smoke/wind's ceil-based full-coverage dispatch grids.
	// The minimum of one keeps the denominator nonzero if a device clamp produces a sub-tile texture extent.
	rGlobalLayout.uiLightTilesX = std::max(1u, static_cast<uint32_t>(fLightingTextureWidth) / shaders::kiComputeTileSize);
	rGlobalLayout.uiLightTilesY = std::max(1u, static_cast<uint32_t>(fLightingTextureHeight) / shaders::kiComputeTileSize);

	// Profile GPU-screen readouts (active pixel dimensions). Deposit rasterizes/clears its whole footprint (not windowed),
	// so report the full deposit extent. Spread processes only the live on-screen visible window in its own texels (mirror
	// of the shadow ray-march sub-window), clamped to the texture extent for the fast-zoom-out transient. The spread
	// textures ramp resolution per pass (gSpreadTextureMultiplierStart at pass 0 -> gSpreadTextureMultiplierEnd at the last
	// active pass), so report both the start-pass and end-pass windows.
	giLightingDepositPixelsX = static_cast<int64_t>(fLightingTextureWidth);
	giLightingDepositPixelsY = static_cast<int64_t>(fLightingTextureHeight);
	XMFLOAT2 f2VisibleAreaNow = area.ComputeVisibleArea(game::gpCamera->mfCameraEyeHeight);
	auto SpreadActivePixels = [&](int64_t iPass, int64_t& riActivePixelsX, int64_t& riActivePixelsY)
	{
		float fSpreadTextureWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpSpreadTextures[iPass][0].mInfo.extent.width);
		float fSpreadTextureHeight = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpSpreadTextures[iPass][0].mInfo.extent.height);
		riActivePixelsX = std::min(static_cast<int64_t>(std::lround(f2VisibleAreaNow.x * fSpreadTextureWidth / area.fFullWidth)), static_cast<int64_t>(fSpreadTextureWidth));
		riActivePixelsY = std::min(static_cast<int64_t>(std::lround(f2VisibleAreaNow.y * fSpreadTextureHeight / area.fFullHeight)), static_cast<int64_t>(fSpreadTextureHeight));
	};
	int64_t iLastSpreadPass = static_cast<int64_t>(gSpreadPassCount.Get()) - 1; // Wrapper range [1, kiMaxSpreadPasses] -> index in [0, kiMaxSpreadPasses - 1]
	SpreadActivePixels(0, giLightingSpreadStartActivePixelsX, giLightingSpreadStartActivePixelsY);
	SpreadActivePixels(iLastSpreadPass, giLightingSpreadEndActivePixelsX, giLightingSpreadEndActivePixelsY);
}

void RenderLightingGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	// Generate run-unique seed once and reuse every frame: stable noise pattern across the run, no temporal flicker.
	static const uint32_t skuiRandomSeed = []
	{
		common::RandomEngine randomEngine;
		randomEngine.TimeSeed();
		return static_cast<uint32_t>(common::RandomNext(randomEngine) >> 32);
	}();
	rGlobalLayout.uiRandomSeed = skuiRandomSeed;

	rGlobalLayout.fLightingObjectsAdd = gLightingObjectsAdd.Get();
	rGlobalLayout.fLightingDepositThreshold = gLightingDepositThreshold.Get();
	rGlobalLayout.fLightingDepositCompress = gLightingDepositCompress.Get();

	rGlobalLayout.fCombineMaxBrightness = gCombineMaxBrightness.Get();
	rGlobalLayout.fCombineContrast = gCombineContrast.Get();
	rGlobalLayout.fCombineLinearStart = gCombineLinearStart.Get();
	rGlobalLayout.fCombineLinearLength = gCombineLinearLength.Get();
	rGlobalLayout.fCombineToe = gCombineToe.Get();
	rGlobalLayout.fCombineBlackTightness = gCombineBlackTightness.Get();
	rGlobalLayout.fCombinePassNormalize = gCombinePassNormalize.Get();
	rGlobalLayout.fCombineExposurePassScale = gCombineExposurePassScale.Get();
	rGlobalLayout.fCombineHuePreserve = gCombineHuePreserve.Get();
	for (int64_t i = 0; i < _countof(rGlobalLayout.pfCombineCurvePoints); ++i)
	{
		float fT = 0.5f;
		if constexpr (shaders::kiMaxSpreadPasses > 1)
		{
			fT = static_cast<float>(i) / static_cast<float>(shaders::kiMaxSpreadPasses - 1);
		}
		rGlobalLayout.pfCombineCurvePoints[i] = (gbUseCombineCurveNew ? gCombineCurveNew : gCombineCurveOld).Evaluate(fT);
	}
	rGlobalLayout.fLightingTerrain = gLightingTerrain.Get();
	rGlobalLayout.fLightingObjects = gLightingObjects.Get();
	rGlobalLayout.fLightingAddTerrain = gLightingAddTerrain.Get();

	// Spread Start
	rGlobalLayout.fSpreadDirectionalityStart = gSpreadDirectionality.Get();
	rGlobalLayout.fSpreadDirectionCountStart = gSpreadDirectionCount.Get();
	rGlobalLayout.fSpreadDistanceStart = gSpreadDistance.Get();
	rGlobalLayout.fSpreadRingCountStart = gSpreadRingCount.Get();
	rGlobalLayout.fSpreadJitterStart = gSpreadJitter.Get();
	rGlobalLayout.fSpreadSampleJitterRangeStart = gSpreadSampleJitterRangeStart.Get();
	rGlobalLayout.fSpreadSampleJitterClusteringStart = gSpreadSampleJitterClusteringStart.Get();
	rGlobalLayout.fSpreadDecayStart = gSpreadDecay.Get();
	rGlobalLayout.fSpreadAccumulationDecayStart = gSpreadAccumulationDecay.Get();
	rGlobalLayout.fSpreadDistanceFalloffStart = gSpreadDistanceFalloff.Get();
	rGlobalLayout.fSpreadOutputThresholdStart = gSpreadOutputThreshold.Get();
	rGlobalLayout.fSpreadOutputCompressStart = gSpreadOutputCompress.Get();
	rGlobalLayout.fSpreadPassCount = gSpreadPassCount.Get();

	// Spread End (interpolation targets for last spread pass)
	rGlobalLayout.fSpreadDirectionalityEnd = gSpreadDirectionalityEnd.Get();
	rGlobalLayout.fSpreadDirectionCountEnd = gSpreadDirectionCountEnd.Get();
	rGlobalLayout.fSpreadDistanceEnd = gSpreadDistanceEnd.Resolve(game::gpCamera->mfCameraEyeHeight);
	rGlobalLayout.fSpreadRingCountEnd = gSpreadRingCountEnd.Get();
	rGlobalLayout.fSpreadJitterEnd = gSpreadJitterEnd.Get();
	rGlobalLayout.fSpreadSampleJitterRangeEnd = gSpreadSampleJitterRangeEnd.Get();
	rGlobalLayout.fSpreadSampleJitterClusteringEnd = gSpreadSampleJitterClusteringEnd.Get();
	rGlobalLayout.fSpreadDecayEnd = gSpreadDecayEnd.Get();
	rGlobalLayout.fSpreadAccumulationDecayEnd = gSpreadAccumulationDecayEnd.Get();
	rGlobalLayout.fSpreadDistanceFalloffEnd = gSpreadDistanceFalloffEnd.Get();
	rGlobalLayout.fSpreadOutputThresholdEnd = gSpreadOutputThresholdEnd.Get();
	rGlobalLayout.fSpreadOutputCompressEnd = gSpreadOutputCompressEnd.Get();

	// Spread Height Fade
	rGlobalLayout.fSpreadHeightMultiplier = gSpreadHeightMultiplier.Get();
	rGlobalLayout.fSpreadHeightEndHeight = gSpreadHeightEndHeight.Get();
	rGlobalLayout.fSpreadHeightPower = gSpreadHeightPower.Get();

	// Per-ring rotation angles: jitter slider sets the seed; the shader scales by interpolated jitter
	// Each ring uses its own seed for uncorrelated rotations
	float fJitter = gSpreadJitter.Get();
	common::RandomEngine ringRandomEngine(1000 * static_cast<uint32_t>(static_cast<float>(shaders::kiMaxSpreadPasses) * fJitter));
	for (int64_t i = 0; i < _countof(rGlobalLayout.pfSpreadRingRotations); ++i)
	{
		rGlobalLayout.pfSpreadRingRotations[i] = common::Random<XM_2PI>(ringRandomEngine);
	}

	// Lighting world-area / temporal / tile / readout population — colocated here so the whole Lighting region lives in one file (region ownership).
	PopulateLightingParameters(rGlobalLayout);
}

void RenderLightingMain(int64_t iCommandBuffer)
{
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rMainLayout.fLightingSampledNormalsOneSize = gLightingSampledNormalsOneSize.Get();
	rMainLayout.fLightingSampledNormalsTwoSize = gLightingSampledNormalsTwoSize.Get();
	rMainLayout.fLightingSampledNormalsThreeSize = gLightingSampledNormalsThreeSize.Get();
	rMainLayout.uiWaterNormalIndexOne = static_cast<uint32_t>(gWaterNormalIndexOne.Get<int64_t>());
	rMainLayout.uiWaterNormalIndexTwo = static_cast<uint32_t>(gWaterNormalIndexTwo.Get<int64_t>());
	rMainLayout.uiWaterNormalIndexThree = static_cast<uint32_t>(gWaterNormalIndexThree.Get<int64_t>());
	// Resolve the 3 normal-weight samples CPU-side by camera eye height and upload one float each
	// Hard-coded fade band (default..2x default eye height) with no author
	// control over the band -> free LerpAtHeight, not a HeightLerpWrapperQuartet; fade endpoint single-sourced on game::Camera.
	rMainLayout.fWaterNormalWeightOne = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightOneMin.Get(), gLightingSampledNormalsWeightOneMax.Get());
	rMainLayout.fWaterNormalWeightTwo = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightTwoMin.Get(), gLightingSampledNormalsWeightTwoMax.Get());
	rMainLayout.fWaterNormalWeightThree = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightThreeMin.Get(), gLightingSampledNormalsWeightThreeMax.Get());
	rMainLayout.fWaterHeightDarkenTop = gWaterHeightDarkenTop.Get();
	rMainLayout.fWaterHeightDarkenBottom = gWaterHeightDarkenBottom.Get();
	rMainLayout.fWaterHeightDarkenTarget = gWaterHeightDarkenTarget.Get();
	rMainLayout.fWaterHeightDarkenSource = gWaterHeightDarkenSource.Get();
	rMainLayout.fWaterHeightDarkenLighting = gWaterHeightDarkenLighting.Get();

	rMainLayout.fLightingWaterSkyboxSunBias = gLightingWaterSkyboxSunBias.Get();
	rMainLayout.fLightingWaterSkyboxNormalBlendWave = gLightingWaterSkyboxNormalBlendWave.Get();
	rMainLayout.fLightingWaterSkyboxIntensity = gLightingWaterSkyboxIntensity.Get();
	rMainLayout.fLightingWaterSkyboxAdd = gLightingWaterSkyboxAdd.Get();
	rMainLayout.fLightingWaterSkyboxOnePower = gLightingWaterSkyboxOnePower.Get();
	rMainLayout.fLightingWaterSkyboxTwo = gLightingWaterSkyboxTwo.Get();
	rMainLayout.fLightingWaterSkyboxTwoPower = gLightingWaterSkyboxTwoPower.Get();
	rMainLayout.fLightingWaterSkyboxThree = gLightingWaterSkyboxThree.Get();
	rMainLayout.fLightingWaterSkyboxThreePower = gLightingWaterSkyboxThreePower.Get();
	rMainLayout.fLightingWaterSkyboxLod = gLightingWaterSkyboxLod.Get();
	rMainLayout.fWaterSpecAAVariance = gWaterSpecAAVariance.Get();
	rMainLayout.fWaterSpecAAThreshold = gWaterSpecAAThreshold.Get();
	rMainLayout.fWaterSpecAAMipScale = gWaterSpecAAMipScale.Get();
	rMainLayout.fWaterNormalMipBias = gWaterNormalMipBias.Get();
	// WATER_SPEC_AA_MIP_HANDOFF: per-mip Toksvig variance tables for the three selected octave-group
	// textures, copied from the header-baked TextureManager tables every frame (30 floats — cheap, and
	// chevron re-selection then needs no separate invalidation path).
	std::memcpy(&rMainLayout.pfWaterSpecAAMipVariance[0 * shaders::kiWaterSpecAAMipTableSize], gpTextureManager->mpfWaterNormalMipVariance[gWaterNormalIndexOne.Get<int64_t>()], shaders::kiWaterSpecAAMipTableSize * sizeof(float));
	std::memcpy(&rMainLayout.pfWaterSpecAAMipVariance[1 * shaders::kiWaterSpecAAMipTableSize], gpTextureManager->mpfWaterNormalMipVariance[gWaterNormalIndexTwo.Get<int64_t>()], shaders::kiWaterSpecAAMipTableSize * sizeof(float));
	std::memcpy(&rMainLayout.pfWaterSpecAAMipVariance[2 * shaders::kiWaterSpecAAMipTableSize], gpTextureManager->mpfWaterNormalMipVariance[gWaterNormalIndexThree.Get<int64_t>()], shaders::kiWaterSpecAAMipTableSize * sizeof(float));
	// Full-detail reference weights for WATER_SPEC_AA_FADE_HANDOFF: the same LerpAtHeight the live
	// fWaterNormalWeight* uploads above use, evaluated at the near-camera endpoint height.
	rMainLayout.fWaterNormalWeightFullOne = engine::LerpAtHeight(game::Camera::kfCameraEyeHeightDefault, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightOneMin.Get(), gLightingSampledNormalsWeightOneMax.Get());
	rMainLayout.fWaterNormalWeightFullTwo = engine::LerpAtHeight(game::Camera::kfCameraEyeHeightDefault, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightTwoMin.Get(), gLightingSampledNormalsWeightTwoMax.Get());
	rMainLayout.fWaterNormalWeightFullThree = engine::LerpAtHeight(game::Camera::kfCameraEyeHeightDefault, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightThreeMin.Get(), gLightingSampledNormalsWeightThreeMax.Get());

	rMainLayout.fLightingWaterReflectedAmount = gLightingWaterReflectedAmount.Get();
	rMainLayout.fLightingWaterReflectedNormalBlendWave = gLightingWaterReflectedNormalBlendWave.Get();
	rMainLayout.fLightingWaterReflectedDistortion = gLightingWaterReflectedDistortion.Get();
	rMainLayout.fLightingWaterReflectedFalloffStart = gLightingWaterReflectedFalloffStart.Get();
	rMainLayout.fLightingWaterReflectedFalloffPower = gLightingWaterReflectedFalloffPower.Get();
	rMainLayout.fLightingWaterReflectedFresnel = gLightingWaterReflectedFresnel.Get();
	rMainLayout.fLightingWaterReflectedIntensity = gLightingWaterReflectedIntensity.Get();

	rMainLayout.fLightingWaterNormalSoften = gLightingWaterNormalSoften.Get();
	rMainLayout.fLightingWaterNormalBlendWave = gLightingWaterNormalBlendWave.Get();
	rMainLayout.fLightingWaterIntensity = gLightingWaterIntensity.Get();
	rMainLayout.fLightingWaterAdd = gLightingWaterAdd.Get();
	rMainLayout.fLightingWaterOne = gLightingWaterOne.Get();
	rMainLayout.fLightingWaterOnePower = gLightingWaterOnePower.Get();
	rMainLayout.fLightingWaterTwo = gLightingWaterTwo.Get();
	rMainLayout.fLightingWaterTwoPower = gLightingWaterTwoPower.Get();
	rMainLayout.fLightingWaterThree = gLightingWaterThree.Get();
	rMainLayout.fLightingWaterThreePower = gLightingWaterThreePower.Get();
	rMainLayout.fLightingWaterPowerMode = gLightingWaterPowerMode.Get();

	rMainLayout.fLightingDirectionalIntensity = gLightingDirectionalIntensity.Get();
	rMainLayout.fLightingDirectionalPower = gLightingDirectionalPower.Get();
	rMainLayout.fLightingDirectionalPowerMode = gLightingDirectionalPowerMode.Get();
	rMainLayout.fLightingAmbientIntensity = gLightingAmbientIntensity.Get();
	rMainLayout.fLightingAmbientPower = gLightingAmbientPower.Get();
	rMainLayout.fLightingAmbientPowerMode = gLightingAmbientPowerMode.Get();
	rMainLayout.fLightingWaterEwnsPow = gLightingWaterEwnsPow.Get();
	rMainLayout.fLightingWaterEwnsPowMode = gLightingWaterEwnsPowMode.Get();
	rMainLayout.fLightingWaterAmbientIntensity = gLightingWaterAmbientIntensity.Get();
	rMainLayout.fLightingWaterAmbientPower = gLightingWaterAmbientPower.Get();
	rMainLayout.fLightingWaterAmbientPowerMode = gLightingWaterAmbientPowerMode.Get();
	rMainLayout.fLightingTerrainBelowBaseMultiplier = gLightingTerrainBelowBaseMultiplier.Get();
	rMainLayout.fLightingTerrainBelowBasePower = gLightingTerrainBelowBasePower.Get();

	// Pbr
	rMainLayout.fPbrExposure = gPbrExposure.Get();
	rMainLayout.fPbrGamma = gPbrGamma.Get();
	rMainLayout.fColorGradingSaturation = gColorGradingSaturation.Get();
	rMainLayout.fColorGradingContrast = gColorGradingContrast.Get();
	rMainLayout.fColorGradingTemperature = gColorGradingTemperature.Get();
	rMainLayout.fPbrDayBrightness = gPbrDayBrightness.Get();
	rMainLayout.fPbrAmbient = gPbrIblAmbient.Get();

	rMainLayout.fPbrMipCount = static_cast<float>(gpTextureManager->mTextureCache.miPbrCubeMipCount);
	rMainLayout.fPbrSmoke = gPbrSmoke.Get();

	rMainLayout.fPbrBrdfDiffuse = gPbrBrdfDiffuse.Get();
	rMainLayout.fPbrBrdfDiffusePower = gPbrBrdfDiffusePower.Get();
	rMainLayout.fPbrBrdfSpecular = gPbrBrdfSpecular.Get();
	rMainLayout.fPbrBrdfSpecularPower = gPbrBrdfSpecularPower.Get();
	rMainLayout.fPbrIblDiffuse = gPbrIblDiffuse.Get();
	rMainLayout.fPbrIblDiffusePower = gPbrIblDiffusePower.Get();
	rMainLayout.fPbrIblSpecular = gPbrIblSpecular.Get();
	rMainLayout.fPbrIblSpecularPower = gPbrIblSpecularPower.Get();
	rMainLayout.fPbrSun = gPbrSun.Get();
	rMainLayout.fPbrLighting = gPbrLighting.Get();
	rMainLayout.fPbrLightingPower = gPbrLightingPower.Get();
	rMainLayout.fPbrLightingSpecular = gPbrLightingSpecular.Get();
	rMainLayout.fPbrLightingSpecularPower = gPbrLightingSpecularPower.Get();
	rMainLayout.fPbrEmissive = gPbrEmissive.Get();
	rMainLayout.fPbrIblShadowBlend = gPbrIblShadowBlend.Get();
	rMainLayout.fPbrIblAmbientColorBlend = gPbrIblAmbientColorBlend.Get();
	rMainLayout.fPbrShadowFloor = gPbrShadowFloor.Get();
	rMainLayout.fPbrCubemapLodPower = gPbrCubemapLodPower.Get();
	rMainLayout.fPbrCubemapLodOffset = gPbrCubemapLodOffset.Get();

	// Smoke shadow
	rMainLayout.fSmokeShadowIntensity = gSmokeShadowIntensity.Get();
}

} // namespace engine

#endif // defined(BT_CLIENT)
