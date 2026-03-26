#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

void RenderLightingGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fLightingDirectional = gLightingDirectional.Get();
	rGlobalLayout.fLightingIndirect = gLightingIndirect.Get();
	rGlobalLayout.fLightingObjectsAdd = gLightingObjectsAdd.Get();
	rGlobalLayout.fDepositEnergyNormalize = gDepositEnergyNormalize.Get();

	rGlobalLayout.fCombineExposure = gCombineExposure.Get();
	rGlobalLayout.fCombinePower = gCombinePower.Get();
	rGlobalLayout.fCombineLinearClamp = gCombineLinearClamp.Get();

	rGlobalLayout.fLightingTerrain = gLightingTerrain.Get();
	rGlobalLayout.fLightingObjects = gLightingObjects.Get();
	rGlobalLayout.fLightingAddTerrain = gLightingAddTerrain.Get();

	// Spread
	rGlobalLayout.fSpreadDirectionality = gSpreadDirectionality.Get();
	rGlobalLayout.fSpreadDirectionCount = gSpreadDirectionCount.Get();
	rGlobalLayout.fSpreadDistance = gSpreadDistance.Get();
	rGlobalLayout.fSpreadRingCount = gSpreadRingCount.Get();
	rGlobalLayout.fSpreadJitter = gSpreadJitter.Get();
	rGlobalLayout.fSpreadDecay = gSpreadDecay.Get();
	rGlobalLayout.fSpreadPassCount = gSpreadPassCount.Get();

	// Per-ring rotation angles: jitter slider sets the seed and scales the result
	// Each ring uses its own seed for uncorrelated rotations
	float fJitter = gSpreadJitter.Get();
	common::RandomEngine ringRng(1000 * static_cast<uint32_t>(20.0f * fJitter));
	for (int64_t i = 0; i < 8; ++i)
	{
		rGlobalLayout.pfSpreadRingRotations[i] = common::Random<360.0f>(ringRng);
	}
}

void RenderLightingMain(int64_t iCommandBuffer)
{
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rMainLayout.fLightingSampledNormalsSize = gLightingSampledNormalsSize.Get();
	rMainLayout.fLightingSampledNormalsSizeMod = gLightingSampledNormalsSizeMod.Get();
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

	rMainLayout.fLightingWaterSpecularDiffuse = gLightingWaterSpecularDiffuse.Get();
	rMainLayout.fLightingWaterSpecularDirect = gLightingWaterSpecularDirect.Get();
	rMainLayout.fLightingWaterSpecular = gLightingWaterSpecular.Get();

	rMainLayout.fLightingWaterSpecularNormalSoften = gLightingWaterSpecularNormalSoften.Get();
	rMainLayout.fLightingWaterSpecularNormalBlendWave = gLightingWaterSpecularNormalBlendWave.Get();
	rMainLayout.fLightingWaterSpecularIntensity = gLightingWaterSpecularIntensity.Get();
	rMainLayout.fLightingWaterSpecularAdd = gLightingWaterSpecularAdd.Get();
	rMainLayout.fLightingWaterSpecularOne = gLightingWaterSpecularOne.Get();
	rMainLayout.fLightingWaterSpecularOnePower = gLightingWaterSpecularOnePower.Get();
	rMainLayout.fLightingWaterSpecularTwo = gLightingWaterSpecularTwo.Get();
	rMainLayout.fLightingWaterSpecularTwoPower = gLightingWaterSpecularTwoPower.Get();
	rMainLayout.fLightingWaterSpecularThree = gLightingWaterSpecularThree.Get();
	rMainLayout.fLightingWaterSpecularThreePower = gLightingWaterSpecularThreePower.Get();

	rMainLayout.fLightingNewDirectional = gLightingNewDirectional.Get();
	rMainLayout.fLightingNewAmbient = gLightingNewAmbient.Get();

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
