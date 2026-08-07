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

static bool IsVisibleAreaInsideHeldCombineCrop(const XMFLOAT4& rf4VisibleArea, const XMFLOAT4& rf4HeldVisibleArea, const XMFLOAT4& rf4HeldLightingArea, float fCombineTextureWidth, float fCombineTextureHeight)
{
	float fHeldCombineTexelX = (rf4HeldLightingArea.z - rf4HeldLightingArea.x) / fCombineTextureWidth;
	float fHeldCombineTexelY = (rf4HeldLightingArea.y - rf4HeldLightingArea.w) / fCombineTextureHeight;
	return rf4VisibleArea.x >= rf4HeldVisibleArea.x - fHeldCombineTexelX
		&& rf4VisibleArea.z <= rf4HeldVisibleArea.z + fHeldCombineTexelX
		&& rf4VisibleArea.y <= rf4HeldVisibleArea.y + fHeldCombineTexelY
		&& rf4VisibleArea.w >= rf4HeldVisibleArea.w - fHeldCombineTexelY;
}

struct LightingTemporalAreaLatch
{
	bool bInitialized = false;
	XMFLOAT4 f4CurrentArea {};
	XMFLOAT4 f4PreviousArea {};
	float fBlend = 1.0f;

	float Update(const XMFLOAT4& rf4CurrentArea, bool& rbReset, float fRequestedBlend, XMFLOAT4& rf4PreviousArea)
	{
		if (rbReset)
		{
			rbReset = false;
			bInitialized = false;
		}

		float fResolvedBlend = fRequestedBlend;
		if (!bInitialized)
		{
			f4CurrentArea = rf4CurrentArea;
			f4PreviousArea = rf4CurrentArea;
			bInitialized = true;
			fResolvedBlend = 1.0f;
		}
		else
		{
			f4PreviousArea = f4CurrentArea;
			f4CurrentArea = rf4CurrentArea;
		}

		rf4PreviousArea = f4PreviousArea;
		fBlend = fResolvedBlend;
		return fBlend;
	}
};

static bool sbLightingRefreshFrame = true; // Cached before global lighting publication so the spread, combine, and temporal chain share one refresh epoch

static void PopulateLightingParameters(shaders::GlobalLayout& rGlobalLayout, bool bScheduledRefresh)
{
	// Lighting area: world-sized texels from a raw-frustum-safe camera-height reference (mirror of the shadow-area path in
	// PopulateShadowParameters). The deposit/spread/combine textures are pre-sized (RenderTargetTextures, via
	// LightingDetailTextureSize) by kfLightingHeadroomMultiplier; the texel world size is sized so a constant
	// on-screen pixel count (textureWidth / kfLightingHeadroomMultiplier) spans the live frustum width at the camera's
	// mfLightingTexelEyeHeight. That reference expands immediately outward and contracts at its existing rate inward,
	// so it never falls below live height; the grid stays fixed at a settled height and snapped cleanly under XY pan.
	// Reading the actual (clamped) extent keeps raw-frustum coverage
	// device-clamp-invariant and snaps deposit quads onto integer texels.
	// f4LightingArea is the full camera-centered footprint snapped to the deposit texel grid. Snapping to
	// the deposit grid (not combine) is load-bearing: deposit is where lights rasterize, so its grid must move in
	// integer-texel steps under pan. Spread/combine/temporal resample the same world rectangle at their own resolutions.
	float fLightingTextureWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.width);
	float fLightingTextureHeight = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpLightingTextures[0].mInfo.extent.height);
	float fCombineTextureWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpCombineTextures[0].mInfo.extent.width);
	float fCombineTextureHeight = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpCombineTextures[0].mInfo.extent.height);
	const XMFLOAT4& rVisibleArea = game::gpCamera->f4RenderVisibleArea;

	WorldSizedTexelArea area = ComputeWorldSizedTexelArea(game::Camera::kfLightingHeadroomMultiplier, game::gpCamera->mfLightingTexelEyeHeight, fLightingTextureWidth, fLightingTextureHeight, gpSwapchainManager->mfAspectRatio, gFov.Get(), game::gpCamera->mVecPosition);

	// Temporal accumulation publishes the current and previous world areas from one refresh epoch; a skip retains both.
	static LightingTemporalAreaLatch sTemporalAreaLatch {};
	static XMFLOAT4 sf4HeldVisibleArea {};
	static bool sbHeldVisibleArea = false;
	sbLightingRefreshFrame = bScheduledRefresh || !sTemporalAreaLatch.bInitialized || !sbHeldVisibleArea
		|| !IsVisibleAreaInsideHeldCombineCrop(rVisibleArea, sf4HeldVisibleArea, sTemporalAreaLatch.f4CurrentArea, fCombineTextureWidth, fCombineTextureHeight);
	if (sbLightingRefreshFrame)
	{
		rGlobalLayout.fLightingTemporalBlend = sTemporalAreaLatch.Update(area.f4Area, gbLightingTemporalReset, gLightingTemporalBlend.Get(), rGlobalLayout.f4LightingAreaPrevious);
		rGlobalLayout.f4LightingArea = sTemporalAreaLatch.f4CurrentArea;
		sf4HeldVisibleArea = rVisibleArea;
		sbHeldVisibleArea = true;
	}
	else
	{
		rGlobalLayout.f4LightingArea = sTemporalAreaLatch.f4CurrentArea;
		rGlobalLayout.f4LightingAreaPrevious = sTemporalAreaLatch.f4PreviousArea;
		rGlobalLayout.fLightingTemporalBlend = sTemporalAreaLatch.fBlend;
	}

	// Lighting-area extent reciprocal (LightingSpread.frag world->texcoord multiply).
	const XMFLOAT4& rLightingArea = sTemporalAreaLatch.f4CurrentArea;
	float fLightingAreaWidth = rLightingArea.z - rLightingArea.x;
	float fLightingAreaHeight = rLightingArea.y - rLightingArea.w;
	rGlobalLayout.f2LightingAreaExtentInv.x = 1.0f / fLightingAreaWidth;
	rGlobalLayout.f2LightingAreaExtentInv.y = 1.0f / fLightingAreaHeight;

	// Edge-fade denominator reciprocal (LightingDepositEdgeFade), deliberately floored unlike smoke/wind's ceil-based
	// full-coverage dispatch grids. The minimum of one keeps the tile count nonzero if a device clamp produces a
	// sub-tile texture extent.
	uint32_t uiLightTilesX = std::max(1u, static_cast<uint32_t>(fLightingTextureWidth) / shaders::kiComputeTileSize);
	uint32_t uiLightTilesY = std::max(1u, static_cast<uint32_t>(fLightingTextureHeight) / shaders::kiComputeTileSize);
	rGlobalLayout.f2LightingDepositSizeInv.x = 1.0f / static_cast<float>(uiLightTilesX * shaders::kiComputeTileSize);
	rGlobalLayout.f2LightingDepositSizeInv.y = 1.0f / static_cast<float>(uiLightTilesY * shaders::kiComputeTileSize);
}

void RenderLightingGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int64_t siLightingRefreshFrame = 0;
	int64_t iLightingUpdateCadence = gLightingUpdateCadence.Get<int64_t>();
	++siLightingRefreshFrame;
	bool bScheduledLightingRefresh = gbLightingTemporalReset || siLightingRefreshFrame % iLightingUpdateCadence == 0;

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

	float fCombineMaxBrightness = gCombineMaxBrightness.Get();
	float fCombineContrast = gCombineContrast.Get();
	float fCombineLinearStart = gCombineLinearStart.Get();
	float fCombineLinearLength = gCombineLinearLength.Get();
	rGlobalLayout.fCombineMaxBrightness = fCombineMaxBrightness;
	rGlobalLayout.fCombineContrast = fCombineContrast;
	rGlobalLayout.fCombineLinearStart = fCombineLinearStart;
	rGlobalLayout.fCombineToe = gCombineToe.Get();
	rGlobalLayout.fCombineBlackTightness = gCombineBlackTightness.Get();
	rGlobalLayout.fCombineHuePreserve = gCombineHuePreserve.Get();

	// Uchimura tone-curve segment constants precomputed on the CPU (all six inputs are invocation-invariant uniforms).
	// S0/S1/CP feed LightCombine.comp Uchimura and DebugTexture.frag; the epsilon guard on P - S1 removes the prior
	// shader divergence (DebugTexture guarded, LightCombine did not).
	float fCombineL0 = ((fCombineMaxBrightness - fCombineLinearStart) * fCombineLinearLength) / fCombineContrast;
	float fCombineS1 = fCombineLinearStart + fCombineContrast * fCombineL0;
	float fCombineC2 = (fCombineContrast * fCombineMaxBrightness) / std::max(fCombineMaxBrightness - fCombineS1, shaders::kfEpsilon);
	rGlobalLayout.fCombineS0 = fCombineLinearStart + fCombineL0;
	rGlobalLayout.fCombineS1 = fCombineS1;
	rGlobalLayout.fCombineCP = -fCombineC2 / fCombineMaxBrightness;

	// Pass normalization / exposure scaling precomputed (fCombinePassNormalize, fCombineExposurePassScale, spread pass count).
	float fPassCount = gSpreadPassCount.Get();
	float fPassNorm = std::lerp(1.0f, 1.0f / fPassCount, gCombinePassNormalize.Get());
	float fPassScale = std::pow(fPassCount, -gCombineExposurePassScale.Get());
	float fCombinePassNormScale = fPassNorm * fPassScale;
	rGlobalLayout.fCombinePassNormScale = fCombinePassNormScale;
	rGlobalLayout.fCombinePassTotalScale = fCombinePassNormScale / fPassCount;
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
	rGlobalLayout.fSpreadHeightEndHeightInv = 1.0f / std::max(gSpreadHeightEndHeight.Get(), 0.001f);
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
	PopulateLightingParameters(rGlobalLayout, bScheduledLightingRefresh);
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
	float fWaterNormalWeightOne = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightOneMin.Get(), gLightingSampledNormalsWeightOneMax.Get());
	float fWaterNormalWeightTwo = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightTwoMin.Get(), gLightingSampledNormalsWeightTwoMax.Get());
	float fWaterNormalWeightThree = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, gLightingSampledNormalsWeightThreeMin.Get(), gLightingSampledNormalsWeightThreeMax.Get());
	rMainLayout.fWaterNormalWeightOne = fWaterNormalWeightOne;
	rMainLayout.fWaterNormalWeightTwo = fWaterNormalWeightTwo;
	rMainLayout.fWaterNormalWeightThree = fWaterNormalWeightThree;
	// Water.frag MIP_HANDOFF / mode-3 weight chain folded CPU-side (the weights are uniform once resolved by eye
	// height): per-octave relative-weight squares and the mode-3 agreement divisor's reciprocal.
	float fWaterNormalWeightTotal = fWaterNormalWeightOne + fWaterNormalWeightTwo + fWaterNormalWeightThree;
	if (fWaterNormalWeightTotal > 0.0f)
	{
		float fInvTotal = 1.0f / fWaterNormalWeightTotal;
		float fWRelOne = fWaterNormalWeightOne * fInvTotal;
		float fWRelTwo = fWaterNormalWeightTwo * fInvTotal;
		float fWRelThree = fWaterNormalWeightThree * fInvTotal;
		rMainLayout.fWaterNormalWRelSqOne = fWRelOne * fWRelOne;
		rMainLayout.fWaterNormalWRelSqTwo = fWRelTwo * fWRelTwo;
		rMainLayout.fWaterNormalWRelSqThree = fWRelThree * fWRelThree;
	}
	else
	{
		rMainLayout.fWaterNormalWRelSqOne = 0.0f;
		rMainLayout.fWaterNormalWRelSqTwo = 0.0f;
		rMainLayout.fWaterNormalWRelSqThree = 0.0f;
	}
	rMainLayout.fWaterNormalWeightSumInv = 1.0f / std::max(3.0f * fWaterNormalWeightTotal, shaders::kfEpsilon);
	// Water.frag height darken: keep bottom, upload only the range reciprocal (top folds away). Unguarded to
	// reproduce the shader's original divide (degenerate top==bottom -> +inf, absorbed by the surrounding clamp).
	float fWaterHeightDarkenTop = gWaterHeightDarkenTop.Get();
	float fWaterHeightDarkenBottom = gWaterHeightDarkenBottom.Get();
	rMainLayout.fWaterHeightDarkenBottom = fWaterHeightDarkenBottom;
	rMainLayout.fWaterHeightDarkenRangeInv = 1.0f / (fWaterHeightDarkenTop - fWaterHeightDarkenBottom);
	rMainLayout.fWaterHeightDarkenTarget = gWaterHeightDarkenTarget.Get();
	rMainLayout.fWaterHeightDarkenSource = gWaterHeightDarkenSource.Get();
	rMainLayout.fWaterHeightDarkenLighting = gWaterHeightDarkenLighting.Get();

	// fLightingWaterSkyboxSunBias no longer uploaded: it folds into globalLayout.f4WaterBiasedSunNormal (GlobalUniforms.cpp).
	rMainLayout.fLightingWaterSkyboxNormalBlendWave = gLightingWaterSkyboxNormalBlendWave.Get();
	rMainLayout.fLightingWaterSkyboxIntensity = gLightingWaterSkyboxIntensity.Get();
	rMainLayout.fLightingWaterSkyboxAdd = gLightingWaterSkyboxAdd.Get();
	float fSkyboxPowerOne = gLightingWaterSkyboxOnePower.Get();
	float fSkyboxPowerTwo = gLightingWaterSkyboxTwoPower.Get();
	float fSkyboxPowerThree = gLightingWaterSkyboxThreePower.Get();
	rMainLayout.fLightingWaterSkyboxOnePower = fSkyboxPowerOne;
	rMainLayout.fLightingWaterSkyboxTwo = gLightingWaterSkyboxTwo.Get();
	rMainLayout.fLightingWaterSkyboxTwoPower = fSkyboxPowerTwo;
	rMainLayout.fLightingWaterSkyboxThree = gLightingWaterSkyboxThree.Get();
	rMainLayout.fLightingWaterSkyboxThreePower = fSkyboxPowerThree;
	rMainLayout.fLightingWaterSkyboxOneBeachReduction = gLightingWaterSkyboxOneBeachReduction.Get();
	rMainLayout.fLightingWaterSkyboxTwoBeachReduction = gLightingWaterSkyboxTwoBeachReduction.Get();
	rMainLayout.fLightingWaterSkyboxThreeBeachReduction = gLightingWaterSkyboxThreeBeachReduction.Get();
	// Per-lobe FilteredPowerLobe constants (Water.frag WATER_SPEC_AA_MODE 2/3): 2/(power+2) and 1/(1+power), xyz = lobes One/Two/Three.
	rMainLayout.f4WaterSkyboxLobeAlphaSq = {2.0f / (fSkyboxPowerOne + 2.0f), 2.0f / (fSkyboxPowerTwo + 2.0f), 2.0f / (fSkyboxPowerThree + 2.0f), 0.0f};
	rMainLayout.f4WaterSkyboxLobeOnePlusPowerInv = {1.0f / (1.0f + fSkyboxPowerOne), 1.0f / (1.0f + fSkyboxPowerTwo), 1.0f / (1.0f + fSkyboxPowerThree), 0.0f};
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
	rMainLayout.fPbrGammaInv = 1.0f / gPbrGamma.Get();
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

void RenderLightingSpreadIndirect(int64_t iCommandBuffer)
{
	// A refresh must draw every spread pass even for an empty deposit so it publishes fresh zero results;
	// skips publish no chain work and retain the prior area/history epoch.
	int64_t iInstanceCount = sbLightingRefreshFrame ? 1 : 0;

	// All kiMaxSpreadPasses pipelines, not just the gSpreadPassCount active ones. Which passes the Main CB
	// actually draws is decided at record time, and the slots come back zeroed from every pipeline recreate
	// (PipelineCreator::CreateHostVisibleIndirectBuffer), so any pass left unwritten here would draw zero
	// instances and black out lighting. Writing the full array keeps this loop independent of the recorded
	// pass count — 40 stores, no reason to make it conditional.
	// Only the current framebuffer's slot (WriteIndirectBuffer indexes by iCommandBuffer), never all
	// miIndirectSlotCount slots: the others belong to frames still in flight on the GPU, and that
	// per-framebuffer slot indexing is what makes this host-visible write race-free.
	for (int64_t iPass = 0; iPass < shaders::kiMaxSpreadPasses; ++iPass)
	{
		gpPipelineManager->mSpreadPipelines[iPass].WriteIndirectBuffer(iCommandBuffer, iInstanceCount);
	}

	// The combine-sized chain covers the whole texture; the refresh predicate suppresses it by zeroing the Z group
	// count, so a skip dispatches nothing without re-recording the Main CB.
	VkExtent3D vkCombineExtent = gpTextureManager->mRenderTargetTextures.mpCombineTextures[0].mInfo.extent;
	int64_t iCombineGroupsX = TileCount(vkCombineExtent.width);
	int64_t iCombineGroupsY = TileCount(vkCombineExtent.height);
	int64_t iCombineGroupsZ = sbLightingRefreshFrame ? 1 : 0;
	gpPipelineManager->mCombinePipeline.WriteIndirectComputeBuffer(iCommandBuffer, iCombineGroupsX, iCombineGroupsY, iCombineGroupsZ);
	gpPipelineManager->mLightingTemporalPipeline.WriteIndirectComputeBuffer(iCommandBuffer, iCombineGroupsX, iCombineGroupsY, iCombineGroupsZ);
	gpPipelineManager->mLightingHistoryCopyPipeline.WriteIndirectComputeBuffer(iCommandBuffer, iCombineGroupsX, iCombineGroupsY, iCombineGroupsZ);
}

} // namespace engine

#endif // defined(BT_CLIENT)
