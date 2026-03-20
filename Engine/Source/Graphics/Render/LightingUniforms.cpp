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
	rGlobalLayout.fLightingCombinePower = gLightingCombinePower.Get();

	rGlobalLayout.fLightingBlurDistance = gLightingBlurDistance.Get();
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	rGlobalLayout.fLightingBlurTextureCount = static_cast<float>(iBlurTextureCount);
	rGlobalLayout.fLightingTerrain = gLightingTerrain.Get();
	rGlobalLayout.fLightingObjects = gLightingObjects.Get();

	rGlobalLayout.fLightingBlurDirectionality = gLightingBlurDirectionality.Get();
	rGlobalLayout.fLightingAddTerrain = gLightingAddTerrain.Get();
	rGlobalLayout.fLightingBlurJitter = gLightingBlurJitter.Get();
	rGlobalLayout.fLightingCombineDecay = gLightingCombineDecay.Get();
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

#endif // BT_CLIENT
