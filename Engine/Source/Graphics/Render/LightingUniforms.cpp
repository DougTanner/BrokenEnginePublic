#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

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
		float fT = (shaders::kiMaxSpreadPasses > 1)
			? static_cast<float>(i) / static_cast<float>(shaders::kiMaxSpreadPasses - 1)
			: 0.5f;
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
	rGlobalLayout.fSpreadDistanceEnd = gSpreadDistanceEnd.Get();
	rGlobalLayout.fSpreadRingCountEnd = gSpreadRingCountEnd.Get();
	rGlobalLayout.fSpreadJitterEnd = gSpreadJitterEnd.Get();
	rGlobalLayout.fSpreadSampleJitterRangeEnd = gSpreadSampleJitterRangeEnd.Get();
	rGlobalLayout.fSpreadSampleJitterClusteringEnd = gSpreadSampleJitterClusteringEnd.Get();
	rGlobalLayout.fSpreadDecayEnd = gSpreadDecay.Get();
	rGlobalLayout.fSpreadAccumulationDecayEnd = gSpreadAccumulationDecay.Get();
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
}

void RenderLightingMain(int64_t iCommandBuffer)
{
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rMainLayout.fLightingSampledNormalsOneSize = gLightingSampledNormalsOneSize.Get();
	rMainLayout.fLightingSampledNormalsTwoSize = gLightingSampledNormalsTwoSize.Get();
	rMainLayout.fLightingSampledNormalsBlend = gLightingSampledNormalsBlend.Get();
	rMainLayout.fLightingSampledNormalsSpeed = gLightingSampledNormalsSpeed.Get();
	rMainLayout.fWaterHeightDarkenTop = gWaterHeightDarkenTop.Get();
	rMainLayout.fWaterHeightDarkenBottom = gWaterHeightDarkenBottom.Get();
	rMainLayout.fWaterHeightDarkenClamp = gWaterHeightDarkenClamp.Get();

	rMainLayout.fLightingWaterSkyboxSunBias = gLightingWaterSkyboxSunBias.Get();
	rMainLayout.fLightingWaterSkyboxNormalSoften = gLightingWaterSkyboxNormalSoften.Get();
	rMainLayout.fLightingWaterSkyboxNormalBlendWave = gLightingWaterSkyboxNormalBlendWave.Get();
	rMainLayout.fLightingWaterSkyboxIntensity = gLightingWaterSkyboxIntensity.Get();
	rMainLayout.fLightingWaterSkyboxAdd = gLightingWaterSkyboxAdd.Get();
	rMainLayout.fLightingWaterSkyboxOnePower = gLightingWaterSkyboxOnePower.Get();
	rMainLayout.fLightingWaterSkyboxTwo = gLightingWaterSkyboxTwo.Get();
	rMainLayout.fLightingWaterSkyboxTwoPower = gLightingWaterSkyboxTwoPower.Get();
	rMainLayout.fLightingWaterSkyboxThree = gLightingWaterSkyboxThree.Get();
	rMainLayout.fLightingWaterSkyboxThreePower = gLightingWaterSkyboxThreePower.Get();
	rMainLayout.fLightingWaterSkyboxLod = gLightingWaterSkyboxLod.Get();

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

	rMainLayout.fLightingNewDirectional = gLightingNewDirectional.Get();
	rMainLayout.fLightingNewDirectionalPower = gLightingNewDirectionalPower.Get();
	rMainLayout.fLightingDirectionalPowerMode = gLightingDirectionalPowerMode.Get();
	rMainLayout.fLightingNewAmbient = gLightingNewAmbient.Get();
	rMainLayout.fLightingNewAmbientPower = gLightingNewAmbientPower.Get();
	rMainLayout.fLightingAmbientPowerMode = gLightingAmbientPowerMode.Get();
	rMainLayout.fLightingWaterAmbientPower = gLightingWaterAmbientPower.Get();
	rMainLayout.fLightingWaterAmbientPowerMode = gLightingWaterAmbientPowerMode.Get();
	rMainLayout.fLightingWaterNewAmbient = gLightingWaterNewAmbient.Get();
	rMainLayout.fLightingWaterNewAmbientPower = gLightingWaterNewAmbientPower.Get();
	rMainLayout.fLightingWaterNewAmbientPowerMode = gLightingWaterNewAmbientPowerMode.Get();
	rMainLayout.fLightingTerrainBelowBaseMultiplier = gLightingTerrainBelowBaseMultiplier.Get();
	rMainLayout.fLightingTerrainBelowBasePower = gLightingTerrainBelowBasePower.Get();

	// Pbr
	rMainLayout.fPbrExposure = gPbrExposure.Get();
	rMainLayout.fPbrGamma = gPbrGamma.Get();
	rMainLayout.fPbrDayBrightness = gPbrDayBrightness.Get();
	rMainLayout.fPbrAmbient = gPbrIblAmbient.Get();

	rMainLayout.fPbrMipCount = static_cast<float>(gpTextureManager->mTextureCache.miPbrCubeMipCount);
	rMainLayout.fPbrDebugViewInputs = 0.0f;
	rMainLayout.fPbrDebugViewEquation = 0.0f;
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
	rMainLayout.fPbrSunPower = gPbrSunPower.Get();
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
