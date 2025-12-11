#include "Render.h"

#include "Graphics/Graphics.h"
#include "Profile/ProfileManager.h"
#include "Ui/WrapperBase.h"

#include "Game.h"
#include "Frame/Frame.h"
#include "Input/Input.h"

namespace engine
{

XMVECTOR XM_CALLCONV DirectionToDirectionMultipliers(FXMVECTOR vecDirection)
{
	XMFLOAT4A f4Direction {};
	XMStoreFloat4A(&f4Direction, vecDirection);

	return XMVectorSet(std::max(f4Direction.x, 0.0f), std::max(-f4Direction.x, 0.0f), std::max(f4Direction.y, 0.0f), std::max(-f4Direction.y, 0.0f));
}

void RenderLightingGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4LightingOne.x = gLightingDirectional.Get();
	rGlobalLayout.f4LightingOne.y = gLightingIndirect.Get();
	rGlobalLayout.f4LightingOne.z = gLightingObjectsAdd.Get();
	rGlobalLayout.f4LightingOne.w = gLightingCombinePower.Get();

	rGlobalLayout.f4LightingTwo.x = gLightingBlurDistance.Get();
	auto [iCombineTextureIndex, iBlurTextureCount] = CombineTextureInfo();
	rGlobalLayout.f4LightingTwo.y = static_cast<float>(iBlurTextureCount);
	rGlobalLayout.f4LightingTwo.z = gLightingTerrain.Get();
	rGlobalLayout.f4LightingTwo.w = gLightingObjects.Get();

	rGlobalLayout.f4LightingThree.x = gLightingBlurDirectionality.Get();
	rGlobalLayout.f4LightingThree.y = gLightingAddTerrain.Get();
	rGlobalLayout.f4LightingThree.z = gLightingBlurJitter.Get();
	rGlobalLayout.f4LightingThree.w = gLightingCombineDecay.Get();
}

void RenderLightingMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate)
{
	float fDayPercent = DayPercent(rFrameInterpolate);

	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rMainLayout.fLightingSampledNormalsSize = gLightingSampledNormalsSize.Get();
	rMainLayout.fLightingSampledNormalsSizeMod = gLightingSampledNormalsSizeMod.Get();
	rMainLayout.fLightingSampledNormalsSpeed = gLightingSampledNormalsSpeed.Get();
	rMainLayout.fWaterHeightDarkenTop = gWaterHeightDarkenTop.Get();
	rMainLayout.fWaterHeightDarkenBottom = gWaterHeightDarkenBottom.Get();
	rMainLayout.fWaterHeightDarkenClamp = gWaterHeightDarkenClamp.Get();

	rMainLayout.fLightingTimeOfDayMultiplier = std::min(fDayPercent + (1.0f - fDayPercent) * gLightingTimeOfDayMultiplier.Get(), 0.85f);

	rMainLayout.fLightingWaterSkyboxSunBias = gLightingWaterSkyboxSunBias.Get();
	rMainLayout.fLightingWaterSkyboxNormalSoften = gLightingWaterSkyboxNormalSoften.Get();
	rMainLayout.fLightingWaterSkyboxNormalBlendWave = gLightingWaterSkyboxNormalBlendWave.Get();
	rMainLayout.fLightingWaterSkyboxIntensity = gLightingWaterSkyboxIntensity.Get();
	rMainLayout.fLightingWaterSkyboxAdd = gLightingWaterSkyboxAdd.Get();
	rMainLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - fDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();
	rMainLayout.fLightingWaterSkyboxOnePower = gLightingWaterSkyboxOnePower.Get();
	rMainLayout.fLightingWaterSkyboxTwo = gLightingWaterSkyboxTwo.Get();
	rMainLayout.fLightingWaterSkyboxTwoPower = gLightingWaterSkyboxTwoPower.Get();
	rMainLayout.fLightingWaterSkyboxThree = gLightingWaterSkyboxThree.Get();
	rMainLayout.fLightingWaterSkyboxThreePower = gLightingWaterSkyboxThreePower.Get();

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

	// Gltf
	rMainLayout.fGltfExposuse = engine::gGltfExposuse.Get();
	rMainLayout.fGltfGamma = std::max(DayPercent(rFrameInterpolate) * engine::gGltfGamma.Get(), 0.001f);
	rMainLayout.fGltfAmbient = engine::gGltfIblAmbient.Get();
	rMainLayout.fGltfDiffuse = gGltfDiffuse.Get();
	rMainLayout.fGltfSpecular = gGltfSpecular.Get();

	rMainLayout.fGltfMipCount = static_cast<float>(engine::gpTextureManager->miGltfCubeMipCount);
	rMainLayout.fGltfDebugViewInputs = 0.0f;
	rMainLayout.fGltfDebugViewEquation = 0.0f;
	rMainLayout.fGltfSmoke = gGltfSmoke.Get();

	rMainLayout.fGltfBrdf = gGltfBrdf.Get();
	rMainLayout.fGltfBrdfPower = gGltfBrdfPower.Get();
	rMainLayout.fGltfIbl = gGltfIbl.Get();
	rMainLayout.fGltfIblPower = gGltfIblPower.Get();
	rMainLayout.fGltfSun = gGltfSun.Get();
	rMainLayout.fGltfSunPower = gGltfSunPower.Get();
	rMainLayout.fGltfLighting = gGltfLighting.Get();
	rMainLayout.fGltfLightingPower = gGltfLightingPower.Get();


}

void RenderFrameGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate)
{
	RenderLightingGlobal(iCommandBuffer);
	RenderSmokeGlobal(iCommandBuffer, rFrameInterpolate);

	float fSunAngle = rFrameInterpolate.fSunAngle;

	// Apply time of day slider override when in Graphics or Tweaks UI
#if defined(ENABLE_DEBUG_INPUT)
	if (game::gpGame->meUiState == game::UiState::kGraphics || game::gpGame->meUiState == game::UiState::kTweaks)
#else
	if (game::gpGame->meUiState == game::UiState::kGraphics)
#endif
	{
		fSunAngle = gSunAngleOverride.Get();
	}

	// Global data
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int siFrame = 0;
	rGlobalLayout.i4Misc.x = static_cast<int>(iCommandBuffer);
	rGlobalLayout.i4Misc.y = static_cast<int>(rFrameInterpolate.iFrame);
	rGlobalLayout.i4Misc.z = static_cast<int>(siFrame++);
	rGlobalLayout.i4Misc.w = static_cast<int>(iCommandBuffer);

	// DT: TODO Time doesn't belong in an individual frame, should it be synced from server?
	//          All of this stuff is not frame based, will eventually need to remove const game::Frame& rFrame parameter, and pass in struct Global?
	static common::Timer sTime;
	rGlobalLayout.f4Misc.x = common::NanosecondsToFloatSeconds<float>(sTime.GetDeltaNs(false));
	rGlobalLayout.f4Misc.y = gBaseHeight.Get();
	rGlobalLayout.f4Misc.z = gpSwapchainManager->mfAspectRatio;
	rGlobalLayout.f4Misc.w = TextureManager::DetailTextureAspectRatio();

	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	// Sun
	auto vecSunNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	auto sunRotationMatrix = XMMatrixRotationY(-fSunAngle);
	vecSunNormal = XMVector4Normalize(XMVector4Transform(vecSunNormal, sunRotationMatrix));
	XMStoreFloat4(&rGlobalLayout.f4SunNormal, vecSunNormal);

	// Sunlight
	float fAmbientNight = std::max(kfDefaultMinimumAmbient, gMinimumAmbient.Get());
	float fAmbientMorning = std::max(0.075f, gMinimumAmbient.Get());
	XMVECTOR vecSunMorning = 0.5f * XMVectorSet(1.0f, 219.0f / 255.0f, 0.f, 1.0f);
	XMVECTOR vecAmbientMorning = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecSunNoon = XMVectorSet(0.8f, 0.8f, 0.8f, 1.0f);
	XMVECTOR vecAmbientNoon = XMVectorSet(0.2f, 0.2f, 0.2f, 1.0f);
	XMVECTOR vecSunEvening = 0.75f * XMVectorSet(0.8f, 0.4f, 0.4f, 1.0f);
	XMVECTOR vecAmbientEvening = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 0.0f);
	XMVECTOR vecSunMidnight = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR vecAmbientMidnight = XMVectorSet(fAmbientNight, fAmbientNight, fAmbientNight, 1.0f);

	XMVECTOR vecSun = vecSunMidnight;
	XMVECTOR vecAmbient = vecAmbientMidnight;

	static constexpr float kfNoonStart = XM_PIDIV8;
	static constexpr float kfNoonEnd = XM_PIDIV2 + XM_PIDIV8;
	static constexpr float kfEvening = XM_PI - XM_PIDIV16;
	static constexpr float kfNightStart = XM_PI;
	static constexpr float kfNightEnd = XM_2PI;
	static constexpr float kfMorning = XM_PIDIV16;

	if (fSunAngle >= kfMorning && fSunAngle < kfNoonStart)
	{
		float fLerp = (fSunAngle - kfMorning) / (kfNoonStart - kfMorning);
		vecSun = XMVectorLerp(vecSunMorning, vecSunNoon, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMorning, vecAmbientNoon, fLerp);
	}
	else if (fSunAngle >= kfNoonStart && fSunAngle < kfNoonEnd)
	{
		float fLerp = (fSunAngle - kfNoonStart) / (kfEvening - kfNoonStart);
		vecSun = XMVectorLerp(vecSunNoon, vecSunNoon, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientNoon, vecAmbientNoon, fLerp);
	}
	else if (fSunAngle >= kfNoonEnd && fSunAngle < kfEvening)
	{
		float fLerp = (fSunAngle - kfNoonEnd) / (kfEvening - kfNoonEnd);
		vecSun = XMVectorLerp(vecSunNoon, vecSunEvening, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientNoon, vecAmbientEvening, fLerp);
	}
	else if (fSunAngle >= kfEvening && fSunAngle < kfNightStart)
	{
		float fLerp = (fSunAngle - kfEvening) / (kfNightStart - kfEvening);
		vecSun = XMVectorLerp(vecSunEvening, vecSunMidnight, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientEvening, vecAmbientMidnight, fLerp);
	}
	else if (fSunAngle >= kfNightStart && fSunAngle < kfNightEnd)
	{
		vecSun = vecSunMidnight;
		vecAmbient = vecAmbientMidnight;
	}
	else if (fSunAngle >= 0.0f)
	{
		float fLerp = fSunAngle / kfMorning;
		vecSun = XMVectorLerp(vecSunMidnight, vecSunMorning, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMidnight, vecAmbientMorning, fLerp);
	}
	else
	{
		DEBUG_BREAK();
	}

	XMStoreFloat4(&rGlobalLayout.f4SunColor, vecSun);
	XMStoreFloat4(&rGlobalLayout.f4AmbientColor, vecAmbient);

	static constexpr float kfNoonFeatherEnd = XM_PIDIV8;
	float fNoonPercent = 0.0f;
	if (fSunAngle >= kfNoonFeatherEnd && fSunAngle <= XM_PIDIV2)
	{
		fNoonPercent = (fSunAngle - kfNoonFeatherEnd) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}
	else if (fSunAngle > XM_PIDIV2 && fSunAngle <= (XM_PI - kfNoonFeatherEnd))
	{
		fNoonPercent = 1.0f - (fSunAngle - XM_PIDIV2) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}

	float fDayPercent = DayPercent(rFrameInterpolate);

	// Shadow texture
	float fShadowTextureSizeWidth = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.width);
	float fShadowTextureSizeHeight = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.height);
	float fShadowElevationTextureSizeWidth = fShadowTextureSizeWidth + 0.5f * fShadowTextureSizeWidth;

	float fShadowNoon = std::pow(fDayPercent, gShadowFeatherPower.Get());
	float fShadowEvening = 1.0f - fShadowNoon;
	float fOffsetNoon = std::pow(fNoonPercent, 2.0f);

	rGlobalLayout.f4ShadowTwo.x = 1.0f / (fShadowNoon * gShadowFeatherNoon.Get() + fShadowEvening * gShadowFeatherSunset.Get());
	rGlobalLayout.f4ShadowTwo.y = fOffsetNoon * gShadowFeatherNoonOffset.Get();
	rGlobalLayout.f4ShadowTwo.z = gShadowDistanceFallof.Get();
	rGlobalLayout.f4ShadowTwo.w = gShadowBlurSigma.Get();
	
	rGlobalLayout.f4ShadowThree.x = fDayPercent * gObjectShadowsBlurDistanceNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsBlurDistanceSunset.Get();
	rGlobalLayout.f4ShadowThree.y = 0.0f;
	rGlobalLayout.f4ShadowThree.z = fDayPercent * gObjectShadowsNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsSunset.Get();
	rGlobalLayout.f4ShadowThree.z *= std::pow(fDayPercent, 0.1f);
	rGlobalLayout.f4ShadowThree.w = fShadowEvening * gShadowFeatherSunsetOffset.Get();

	static constexpr float kfSunriseStretchBegin = XM_2PI - XM_PIDIV4;
	static constexpr float kfSunriseStretchEnd = XM_PIDIV2 - XM_PIDIV16;
	static constexpr float kfSunriseStretchTotal = (XM_2PI - kfSunriseStretchBegin) + kfSunriseStretchEnd;
	float fSunriseStretch = 0.0f;
	static constexpr float kfSunsetStretchBegin = XM_PIDIV2 + XM_PIDIV16;
	static constexpr float kfSunsetStretchEnd = XM_PI + XM_PIDIV4;
	static constexpr float kfSunsetStretchTotal = kfSunsetStretchEnd - kfSunsetStretchBegin;
	float fSunsetStretch = 0.0f;
	if (fSunAngle >= kfSunriseStretchBegin && fSunAngle < 0.0f)
	{
		fSunriseStretch = 1.0f - (fSunAngle - kfSunriseStretchBegin) / (kfSunriseStretchTotal);
	}
	else if (fSunAngle >= 0.0f && fSunAngle < kfSunriseStretchEnd)
	{
		fSunriseStretch = 1.0f - (fSunAngle + (XM_2PI - kfSunriseStretchBegin)) / kfSunriseStretchTotal;
	}
	else if (fSunAngle >= kfSunriseStretchEnd && fSunAngle < kfSunsetStretchBegin)
	{
	}
	else if (fSunAngle >= kfSunsetStretchBegin && fSunAngle < kfSunsetStretchEnd)
	{
		fSunsetStretch = (fSunAngle - kfSunsetStretchBegin) / kfSunsetStretchTotal;
	}
	else if (fSunAngle >= kfSunsetStretchEnd)
	{
		fSunriseStretch = 1.0f;
		fSunsetStretch = 1.0f;
	}

	rGlobalLayout.f4ShadowFour.x = fSunriseStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.f4ShadowFour.y = fSunsetStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.f4ShadowFour.z = std::pow(fDayPercent, 0.25f) * gShadowAffectAmbient.Get();
	rGlobalLayout.f4ShadowFour.w = 0.0f;

	rGlobalLayout.fShadowTextureSizeWidth = fShadowTextureSizeWidth;
	rGlobalLayout.fShadowTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowElevationTextureSizeWidth = fShadowElevationTextureSizeWidth;
	rGlobalLayout.fShadowElevationTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowHeightFadeTop = gShadowHeightFadeTop.Get();
	rGlobalLayout.fShadowHeightFadeBottom = gShadowHeightFadeBottom.Get();

	rGlobalLayout.i4ShadowTwo.x = gpTextureManager->mShadowTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.i4ShadowTwo.y = gpTextureManager->mShadowTexture.mInfo.extent.height; // Y pixels
	rGlobalLayout.i4ShadowTwo.z = gpTextureManager->mObjectShadowsTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.i4ShadowTwo.w = gpTextureManager->mObjectShadowsTexture.mInfo.extent.height; // Y pixels

	rGlobalLayout.f4VisibleAreaShadowsExtra = game::gpCamera->f4RenderVisibleArea;
	float fQuads = (game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / game::gpCamera->f2VisibleAreaQuadSize.x;
	if (fSunAngle >= XM_PI + XM_PIDIV2 || fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.z += (fQuads / 2.0f) * game::gpCamera->f2VisibleAreaQuadSize.x;

		rGlobalLayout.f4ShadowOne.x = (game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
		{
			rGlobalLayout.f4ShadowOne.y = fSunAngle;
		}
		else
		{
			rGlobalLayout.f4ShadowOne.y = 0.0f;
		}
		rGlobalLayout.f4ShadowOne.z = 1.0;

		rGlobalLayout.i4ShadowOne.x = static_cast<int>(fShadowElevationTextureSizeWidth); // !=
		rGlobalLayout.i4ShadowOne.y = 1; // ++
		rGlobalLayout.i4ShadowOne.z = 0; // Start offset
	}
	else
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.x -= (fQuads / 2.0f) * game::gpCamera->f2VisibleAreaQuadSize.x;

		rGlobalLayout.f4ShadowOne.x = -(game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		rGlobalLayout.f4ShadowOne.y = fSunAngle >= XM_PI ? 0.0f : XM_PI - fSunAngle;
		rGlobalLayout.f4ShadowOne.z = -1.0;

		rGlobalLayout.i4ShadowOne.x = 0; // !=
		rGlobalLayout.i4ShadowOne.y = -1; // ++
		rGlobalLayout.i4ShadowOne.z = static_cast<int>(fShadowTextureSizeWidth / 2.0f); // Start offset
	}

	// Terrain
	rGlobalLayout.f4Terrain.x = gIslandHeight.Get();
	rGlobalLayout.f4Terrain.y = fNoonPercent * gIslandAmbientOcclusion.Get();
	rGlobalLayout.f4Terrain.z = gTerrainEarlyOut.Get();
	rGlobalLayout.f4Terrain.w = gWaterEarlyOut.Get();

	rGlobalLayout.f4TerrainTwo.x = gWaterDepth.Get();
	rGlobalLayout.f4TerrainTwo.y = std::max(0.25f, std::pow(fDayPercent, 0.25f));
	rGlobalLayout.f4TerrainTwo.z = 0.0f;
	rGlobalLayout.f4TerrainTwo.w = 0.0f;

	rGlobalLayout.fTerrainNormalXMultiplier = gpIslands->mbFlipX ? -1.0f : 1.0f;
	rGlobalLayout.fTerrainNormalYMultiplier = gpIslands->mbFlipY ? -1.0f : 1.0f;

	rGlobalLayout.fTerrainSnowMultiplier = gTerrainSnowMultiplier.Get();

	rGlobalLayout.fTerrainRockMultiplier = gTerrainRockMultiplier.Get();
	rGlobalLayout.fTerrainRockSize = gTerrainRockSize.Get();
	rGlobalLayout.fTerrainRockBlend = gTerrainRockBlend.Get();
	rGlobalLayout.fTerrainRockNormalsSizeOne = gTerrainRockNormalsSizeOne.Get();
	rGlobalLayout.fTerrainRockNormalsSizeTwo = gTerrainRockNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainRockNormalsSizeThree = gTerrainRockNormalsSizeThree.Get();
	rGlobalLayout.fTerrainRockNormalsBlend = gTerrainRockNormalsBlend.Get();

	rGlobalLayout.fTerrainBeachHeight = gTerrainBeachHeight.Get();
	rGlobalLayout.fTerrainBeachSandSize = gTerrainBeachSandSize.Get();
	rGlobalLayout.fTerrainBeachSandBlend = gTerrainBeachSandBlend.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeOne = gTerrainBeachNormalsSizeOne.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeTwo = gTerrainBeachNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeThree = gTerrainBeachNormalsSizeThree.Get();
	rGlobalLayout.fTerrainBeachNormalsBlend = std::max(fDayPercent * fDayPercent, 0.25f) * gTerrainBeachNormalsBlend.Get();

	// Water global
	rGlobalLayout.f4WaterOne.x = gWaterTerrainHeight.Get();
	rGlobalLayout.f4WaterOne.y = gWaterTerrainFade.Get();
	rGlobalLayout.f4WaterOne.z = gWaterNoiseFrequency.Get();
	rGlobalLayout.f4WaterOne.w = gWaterNoiseAmount.Get();

	rGlobalLayout.f4WaterTwo.x = gWaterDepthLutFeather.Get();
	rGlobalLayout.f4WaterTwo.y = gWaterDepthColorFeather.Get();
	rGlobalLayout.f4WaterTwo.z = fDayPercent * gWaterDepthReflectionFeather.Get();
	rGlobalLayout.f4WaterTwo.w = gWaterColorNoiseFrequency.Get();
	
	rGlobalLayout.f4WaterThree.x = gHighMultiplier.Get();
	rGlobalLayout.f4WaterThree.y = gHighScaleOne.Get();
	rGlobalLayout.f4WaterThree.z = gHighScaleTwo.Get();

	if (fSunAngle >= XM_PIDIV16 && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4WaterThree.w = 1.0f - (fSunAngle - XM_PIDIV16) / (XM_PIDIV2 - XM_PIDIV16);
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI - XM_PIDIV16)
	{
		rGlobalLayout.f4WaterThree.w = (fSunAngle - XM_PIDIV2) / (XM_PI - XM_PIDIV16 - XM_PIDIV2);
	}
	else
	{
		rGlobalLayout.f4WaterThree.w = 1.0f;
	}
	rGlobalLayout.f4WaterThree.w = std::pow(rGlobalLayout.f4WaterThree.w, 2.0f);

	rGlobalLayout.f4WaterFour.x = std::pow(fDayPercent, 0.5f) * gWaterFresnel.Get();
	rGlobalLayout.f4WaterFour.y = gWaterColorBottom.Get();
	rGlobalLayout.f4WaterFour.z = 1.0f / gWaterColorHeight.Get();
	rGlobalLayout.f4WaterFour.w = gWaterColorNoiseAmount.Get();

	if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4WaterFive.w = 1.0f - (fSunAngle) / XM_PIDIV2;
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI)
	{
		rGlobalLayout.f4WaterFive.w = (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		rGlobalLayout.f4WaterFive.w = 1.0f;
	}
	rGlobalLayout.f4WaterFive.w = std::pow(rGlobalLayout.f4WaterFive.w, 2.0f);

	rGlobalLayout.f4WaterSix.x = std::pow(fDayPercent, 0.5f) * gWaterFresnel2.Get();
	rGlobalLayout.f4WaterSix.y = gBeachDirectionalFadeBottom.Get();
	rGlobalLayout.f4WaterSix.z = 1.0f / gBeachDirectionalFadeHeight.Get();
	rGlobalLayout.f4WaterSix.w = gLowSteepness.Get();

	rGlobalLayout.f4WaterSeven.x = gMediumSteepness.Get();

	rGlobalLayout.i4Water.x = static_cast<int>(std::min(gLowCount.Get<int64_t>(), static_cast<int64_t>(gLowMax.Get())));
	rGlobalLayout.i4Water.y = static_cast<int>(gMediumCount.Get<int64_t>());
}

void RenderFrameMain(int64_t iCommandBuffer, const game::FrameInterpolate& rFrameInterpolate)
{
	ASSERT(rFrameInterpolate.eFrameType == FrameType::kInterpolate || rFrameInterpolate.iFrame == 0);

	RenderLightingMain(iCommandBuffer, rFrameInterpolate);
	game::FrameInterpolate::Render(rFrameInterpolate, iCommandBuffer);

	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	// Camera shake
 	float fCameraShake = std::pow(game::gpCamera->mfShake, 1.0f);
	constexpr float kfMaxRoll = 0.005f;
	constexpr float kfMaxPitch = 0.005f;
	constexpr float kfMaxYaw = 0.01f;
	siv::BasicPerlinNoise<float> perlinRoll {0};
	siv::BasicPerlinNoise<float> perlinPitch {1};
	siv::BasicPerlinNoise<float> perlinYaw {2};
	auto matCameraShake = XMMatrixRotationRollPitchYaw(kfMaxRoll * fCameraShake * (-1.0f + 2.0f * perlinRoll.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxPitch * fCameraShake * (-1.0f + 2.0f * perlinPitch.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)), kfMaxYaw * fCameraShake * (-1.0f + 2.0f * perlinYaw.octave1D_01(8.0f * rFrameInterpolate.fCurrentTime, 4)));

	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&rMainLayout.f4x4ViewProjection[0]), XMMatrixTranspose(XMMatrixMultiply(game::gpCamera->mMatView, XMMatrixMultiply(matCameraShake, game::gpCamera->mMatPerspective))));

	XMStoreFloat4(&rMainLayout.f4EyePosition, game::gpCamera->mVecEyePosition);
	XMStoreFloat4(&rMainLayout.f4ToEyeNormal, game::gpCamera->mVecToEyeNormal);

	// Water low frequency
	{
		int64_t iCount = gLowCount.Get<int64_t>();

		auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gLowAngle.Get()));
		rMainLayout.pf4LowWavesOne[0].x = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].y = XMVectorGetY(vecDirection);

		vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(0.0f));
		rMainLayout.pf4LowWavesOne[0].z = XMVectorGetX(vecDirection);
		rMainLayout.pf4LowWavesOne[0].w = XMVectorGetY(vecDirection);

		rMainLayout.pf4LowWavesTwo[0].x = (2.0f * XM_PI) / (gLowWavelength.Get()); // Omega
		rMainLayout.pf4LowWavesTwo[0].y = gLowAmplitude.Get();
		rMainLayout.pf4LowWavesTwo[0].z = gLowSpeed.Get() * rMainLayout.pf4LowWavesTwo[0].x; // Phi
		rMainLayout.pf4LowWavesTwo[0].w = 0.0f;

		common::RandomEngine randomEngine {};
		for (int64_t i = 1; i < iCount; ++i)
		{
			float fAngleAdjust = ((i % 2) == 0 ? 1.0f : -1.0f) * gLowAngleAdjust.Get() * common::Random(randomEngine);
			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(gLowAngle.Get() + fAngleAdjust));
			rMainLayout.pf4LowWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].y = XMVectorGetY(vecDirection);

			vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(XM_2PI * static_cast<float>(i) / static_cast<float>(iCount)));
			rMainLayout.pf4LowWavesOne[i].z = XMVectorGetX(vecDirection);
			rMainLayout.pf4LowWavesOne[i].w = XMVectorGetY(vecDirection);

			float fAdjust = common::Random(randomEngine); // static_cast<float>(i) / static_cast<float>(iCount - 1);
			float fWavelengthAdjust = fAdjust * gLowWavelengthAdjust.Get();
			float fAmplitudeAdjust = (1.0f - fAdjust) * std::abs(gLowAmplitudeAdjust.Get()) * common::Random(randomEngine);
			float fSpeedAdjust = fAdjust * gLowSpeedAdjust.Get();
			rMainLayout.pf4LowWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gLowWavelength.Get() + fWavelengthAdjust * gLowWavelength.Get())); // Omega
			rMainLayout.pf4LowWavesTwo[i].y = std::abs(gLowAmplitude.Get() - fAmplitudeAdjust * gLowAmplitude.Get());
			rMainLayout.pf4LowWavesTwo[i].y = std::min(rMainLayout.pf4LowWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4LowWavesTwo[i].x));
			rMainLayout.pf4LowWavesTwo[i].z = (gLowSpeed.Get() + gLowSpeed.Get() * fSpeedAdjust * common::Random(randomEngine)) * rMainLayout.pf4LowWavesTwo[i].x; // Phi
			rMainLayout.pf4LowWavesTwo[i].w = 0.0f; // common::Random(randomEngine);

			if (i < 64 && (i % 3) == 0)
			{
				rMainLayout.pf4LowWavesTwo[i].y = 0.0f;
			}
		}
	}

	// Water medium frequency
	{
		int64_t iCount = gMediumCount.Get<int64_t>();

		common::RandomEngine randomEngine {};
		for (int64_t i = 0; i < iCount; ++i)
		{
			float fAngleAdjust = gMediumAngleAdjust.Get() * common::Random(randomEngine);
			auto vecDirection = XMVector3Transform(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationZ(fAngleAdjust));
			rMainLayout.pf4MediumWavesOne[i].x = XMVectorGetX(vecDirection);
			rMainLayout.pf4MediumWavesOne[i].y = XMVectorGetY(vecDirection);

			float fWavelengthAdjust = -gMediumWavelengthAdjust.Get() + 2.0f * gMediumWavelengthAdjust.Get() * common::Random(randomEngine);
			float fAmplitudeAdjust = -gMediumAmplitudeAdjust.Get() + 2.0f * gMediumAmplitudeAdjust.Get() * common::Random(randomEngine);
			float fSpeedAdjust = -gMediumSpeedAdjust.Get() + 2.0f * gMediumSpeedAdjust.Get() * common::Random(randomEngine);
			rMainLayout.pf4MediumWavesTwo[i].x = std::abs((2.0f * XM_PI) / (gMediumWavelength.Get() + fWavelengthAdjust * gMediumWavelength.Get())); // Omega
			rMainLayout.pf4MediumWavesTwo[i].y = std::abs(gMediumAmplitude.Get() + fAmplitudeAdjust * gMediumAmplitude.Get());
			rMainLayout.pf4MediumWavesTwo[i].y = std::min(rMainLayout.pf4MediumWavesTwo[i].y, 0.1f * (1.0f / rMainLayout.pf4MediumWavesTwo[i].x));
			rMainLayout.pf4MediumWavesTwo[i].z = (gMediumSpeed.Get() + fSpeedAdjust * gMediumSpeed.Get()) * rMainLayout.pf4MediumWavesTwo[i].x; // Phi
			rMainLayout.pf4MediumWavesTwo[i].w = 0.0f; // gMediumSteepness.Get();
		}
	}

	// Hex shield
	rMainLayout.fHexShieldGrow = gHexShieldGrow.Get();
	rMainLayout.fHexShieldEdgeDistance = gHexShieldEdgeDistance.Get();
	rMainLayout.fHexShieldEdgePower = gHexShieldEdgePower.Get();
	rMainLayout.fHexShieldEdgeMultiplier = gHexShieldEdgeMultiplier.Get();

	rMainLayout.fHexShieldWaveMultiplier = gHexShieldWaveMultiplier.Get();
	rMainLayout.fHexShieldWaveDotMultiplier = gHexShieldWaveDotMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityMultiplier = gHexShieldWaveIntensityMultiplier.Get();
	rMainLayout.fHexShieldWaveIntensityPower = gHexShieldWaveIntensityPower.Get();
	rMainLayout.fHexShieldWaveFalloffPower = gHexShieldWaveFalloffPower.Get();

	rMainLayout.fHexShieldDirectionFalloffPower = gHexShieldDirectionFalloffPower.Get();
	rMainLayout.fHexShieldDirectionMultiplier = gHexShieldDirectionMultiplier.Get();
}

void XM_CALLCONV RenderObjects(shaders::ObjectLayout* pLayouts, int64_t iCommandBuffer, int64_t iCount, const XMVECTOR* pVecPositions, const XMVECTOR* pVecDirections, FXMMATRIX matScale, CXMMATRIX matRotation, [[maybe_unused]] CpuCounters eCounter, Pipelines ePipeline, Pipelines ePipelineShadow)
{
	PROFILE_SET_COUNT(eCounter, iCount);

	int64_t iRendered = 0;
	for (int64_t i = 0; i < iCount; ++i)
	{
		auto& rVecPosition = pVecPositions[i];

		XMFLOAT4A f4Position{};
		XMStoreFloat4A(&f4Position, rVecPosition);
		if (f4Position.x < game::gpCamera->f4RenderVisibleArea.x || f4Position.x > game::gpCamera->f4RenderVisibleArea.z || f4Position.y > game::gpCamera->f4RenderVisibleArea.y || f4Position.y < game::gpCamera->f4RenderVisibleArea.w)
		{
			continue;
		}

		auto vecDirection = XMVector3Normalize(pVecDirections[i]);
		auto matRotationFinal = matRotation * common::RotationMatrixFromDirection(XMVectorNegate(vecDirection), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));
		auto matTranslation = XMMatrixTranslationFromVector(rVecPosition);
		auto matTransform = matScale * matRotationFinal * matTranslation;

		shaders::ObjectLayout& rObjectLayout = pLayouts[iRendered];
		rObjectLayout.ui4Misc = { 0xFFFFFFFF, 0, 0, 0 };
		rObjectLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

		++iRendered;
	}
	PROFILE_SET_COUNT(eCounter + 1, iRendered);

	gpPipelineManager->mpPipelines[ePipeline].WriteIndirectBuffer(iCommandBuffer, iRendered);
	if (ePipelineShadow != kPipelineCount)
	{
		gpPipelineManager->mpPipelines[ePipelineShadow].WriteIndirectBuffer(iCommandBuffer, iRendered);
	}
}

// 60 updates per second
constexpr float kfSmokeUpdateInterval = 0.0166666657f;

static XMFLOAT4 sf4SmokeArea {};

void RenderSmokeGlobal(int64_t iCommandBuffer, const game::FrameInterpolate& __restrict rFrameInterpolate)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.f4SmokeOne.x = kfSmokeUpdateInterval;
	rGlobalLayout.f4SmokeOne.y = gSmokeMax.Get();
	rGlobalLayout.f4SmokeOne.z = gSmokePower.Get();
	rGlobalLayout.f4SmokeOne.w = gSmokeDecay.Get();

	rGlobalLayout.f4SmokeTwo.x = gSmokeColorMin.Get();
	rGlobalLayout.f4SmokeTwo.y = gSmokeColorMultiplier.Get();
	rGlobalLayout.f4SmokeTwo.z = gSmokeTrailsFalloff.Get();
	rGlobalLayout.f4SmokeTwo.w = gSmokeDecayExtra.Get();

	rGlobalLayout.f4SmokeThree.x = gSmokeDecayExtraThreshold.Get();
	rGlobalLayout.f4SmokeThree.y = gSmokeWindNoiseScale.Get();
	rGlobalLayout.f4SmokeThree.z = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.f4SmokeThree.w = gSmokeNoiseQuantity.Get();

	rGlobalLayout.f4SmokeFour.x = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.f4SmokeFour.y = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.f4SmokeFour.z = 0.0f; // (6144.0f / SmokeSimulationPixels()) * 0.75f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeFour.w = 1.0f / gSmokeEdgeDecayDistance.Get();

	static bool sbSmoke = false;
	if (sbSmoke != gSmoke.Get<bool>())
	{
		sbSmoke = gSmoke.Get<bool>();
		gbSmokeClear = true;
	}

	if (gbSmokeClear)
	{
		gbSmokeClear = false;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	gbSmokeSpread = true; // DT: TODO
	if (!gbSmokeSpread || !gSmoke.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	gbSmokeSpread = false;

	XMFLOAT4A f4PlayerPosition {};
	XMStoreFloat4A(&f4PlayerPosition, rFrameInterpolate.player.vecPosition);
	float fAreaX = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	float fAreaY = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeArea = {f4PlayerPosition.x - fAreaX, f4PlayerPosition.y + fAreaY, f4PlayerPosition.x + fAreaX, f4PlayerPosition.y - fAreaY};
	sf4SmokeArea = rGlobalLayout.f4SmokeArea;

	float fXOffset = (sf4PreviousSmokeArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousSmokeArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousSmokeArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousSmokeArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokeSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Misc = {};
	sf4PreviousSmokeArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearOne].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearTwo].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadTwo].WriteIndirectBuffer(iCommandBuffer, 1);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadOne].WriteIndirectBuffer(iCommandBuffer, 1);
}

}
