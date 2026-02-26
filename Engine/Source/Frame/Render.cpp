#ifdef BT_CLIENT

#include "Render.h"

#include "Graphics/Graphics.h"
#include "Graphics/Islands.h"
#include "Graphics/Managers/BufferManager.h"
#include "Graphics/Managers/PipelineManager.h"
#include "Graphics/Managers/SwapchainManager.h"
#include "Graphics/Managers/TextureManager.h"

#include "Game.h"
#include "Graphics/Camera.h"
#include "Input/Input.h"
#include "Profile/ProfileManager.h"

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

void RenderLightingMain(int64_t iCommandBuffer, [[maybe_unused]] const game::FrameInterpolate& rFrameInterpolate)
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

	rMainLayout.fPbrMipCount = static_cast<float>(gpTextureManager->miPbrCubeMipCount);
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

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime, int64_t iFrame)
{
	RenderLightingGlobal(iCommandBuffer);
	RenderSmokeGlobal(iCommandBuffer);
	RenderWindGlobal(iCommandBuffer);

	float fSunAngle = game::gpCamera->SunAngle();

	// Global data
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.iCommandBuffer = static_cast<int>(iCommandBuffer);
	rGlobalLayout.iCameraFrame = static_cast<int>(game::gpCamera->miFrame);
	rGlobalLayout.iFrameCounter = static_cast<int>(iFrame);
	rGlobalLayout.iCommandBufferPad = static_cast<int>(iCommandBuffer);

	rGlobalLayout.fElapsedTime = fCurrentTime;
	rGlobalLayout.fBaseHeight = gBaseHeight.Get();
	rGlobalLayout.fAspectRatio = gpSwapchainManager->mfAspectRatio;
	rGlobalLayout.fDetailTextureAspectRatio = TextureManager::DetailTextureAspectRatio();

	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	// Sun
	XMVECTOR vecSunNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	XMMATRIX matSunRotation = XMMatrixRotationY(-fSunAngle);
	vecSunNormal = XMVector4Normalize(XMVector4Transform(vecSunNormal, matSunRotation));
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

	float fDayPercent = 0.0f;
	if (fSunAngle >= 0.0f && fSunAngle <= XM_PIDIV2)
	{
		fDayPercent = fSunAngle / XM_PIDIV2;
	}
	else if (fSunAngle > XM_PIDIV2 && fSunAngle <= XM_PI)
	{
		fDayPercent = 1.0f - (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}

	// Shadow texture
	float fShadowTextureSizeWidth = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.width);
	float fShadowTextureSizeHeight = static_cast<float>(gpTextureManager->mShadowTexture.mInfo.extent.height);
	float fShadowElevationTextureSizeWidth = fShadowTextureSizeWidth + 0.5f * fShadowTextureSizeWidth;

	float fShadowNoon = std::pow(fDayPercent, gShadowFeatherPower.Get());
	float fShadowEvening = 1.0f - fShadowNoon;
	float fOffsetNoon = std::pow(fNoonPercent, 2.0f);

	rGlobalLayout.fShadowFeather = 1.0f / (fShadowNoon * gShadowFeatherNoon.Get() + fShadowEvening * gShadowFeatherSunset.Get());
	rGlobalLayout.fShadowNoonOffset = fOffsetNoon * gShadowFeatherNoonOffset.Get();
	rGlobalLayout.fShadowDistanceFalloff = gShadowDistanceFallof.Get();
	rGlobalLayout.fShadowBlurSigma = gShadowBlurSigma.Get();
	
	rGlobalLayout.fObjectShadowsBlurDistance = fDayPercent * gObjectShadowsBlurDistanceNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsBlurDistanceSunset.Get();
	rGlobalLayout.fShadowThreePadY = 0.0f;
	rGlobalLayout.fObjectShadowsIntensity = fDayPercent * gObjectShadowsNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsSunset.Get();
	rGlobalLayout.fObjectShadowsIntensity *= std::pow(fDayPercent, 0.1f);
	rGlobalLayout.fShadowSunsetOffset = fShadowEvening * gShadowFeatherSunsetOffset.Get();

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

	rGlobalLayout.fShadowSunriseStretch = fSunriseStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.fShadowSunsetStretch = fSunsetStretch * gObjectShadowsSunsetStretch.Get();
	rGlobalLayout.fShadowAffectAmbient = std::pow(fDayPercent, 0.25f) * gShadowAffectAmbient.Get();
	rGlobalLayout.fShadowFourPadW = 0.0f;

	rGlobalLayout.fShadowTextureSizeWidth = fShadowTextureSizeWidth;
	rGlobalLayout.fShadowTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowElevationTextureSizeWidth = fShadowElevationTextureSizeWidth;
	rGlobalLayout.fShadowElevationTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowHeightFadeTop = gShadowHeightFadeTop.Get();
	rGlobalLayout.fShadowHeightFadeBottom = gShadowHeightFadeBottom.Get();

	rGlobalLayout.iShadowTextureWidth = gpTextureManager->mShadowTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.iShadowTextureHeight = gpTextureManager->mShadowTexture.mInfo.extent.height; // Y pixels
	rGlobalLayout.iObjectShadowTextureWidth = gpTextureManager->mObjectShadowsTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.iObjectShadowTextureHeight = gpTextureManager->mObjectShadowsTexture.mInfo.extent.height; // Y pixels

	rGlobalLayout.f4VisibleAreaShadowsExtra = game::gpCamera->f4RenderVisibleArea;
	float fQuads = (game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / game::gpCamera->f2VisibleAreaQuadSize.x;
	if (fSunAngle >= XM_PI + XM_PIDIV2 || fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.z += (fQuads / 2.0f) * game::gpCamera->f2VisibleAreaQuadSize.x;

		rGlobalLayout.fShadowWidthScale = (game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
		{
			rGlobalLayout.fShadowSunAngle = fSunAngle;
		}
		else
		{
			rGlobalLayout.fShadowSunAngle = 0.0f;
		}
		rGlobalLayout.fShadowDirectionMultiplier = 1.0f;

		rGlobalLayout.iShadowElevationSize = static_cast<int>(fShadowElevationTextureSizeWidth); // !=
		rGlobalLayout.iShadowIncrement = 1; // ++
		rGlobalLayout.iShadowStartOffset = 0; // Start offset
	}
	else
	{
		rGlobalLayout.f4VisibleAreaShadowsExtra.x -= (fQuads / 2.0f) * game::gpCamera->f2VisibleAreaQuadSize.x;

		rGlobalLayout.fShadowWidthScale = -(game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x) / fShadowTextureSizeWidth;
		rGlobalLayout.fShadowSunAngle = fSunAngle >= XM_PI ? 0.0f : XM_PI - fSunAngle;
		rGlobalLayout.fShadowDirectionMultiplier = -1.0f;

		rGlobalLayout.iShadowElevationSize = 0; // !=
		rGlobalLayout.iShadowIncrement = -1; // ++
		rGlobalLayout.iShadowStartOffset = static_cast<int>(fShadowTextureSizeWidth / 2.0f); // Start offset
	}

	// Terrain
	rGlobalLayout.fIslandHeight = gIslandHeight.Get();
	rGlobalLayout.fIslandAmbientOcclusion = fNoonPercent * gIslandAmbientOcclusion.Get();
	rGlobalLayout.fTerrainEarlyOut = gTerrainEarlyOut.Get();
	rGlobalLayout.fWaterEarlyOut = gWaterEarlyOut.Get();

	rGlobalLayout.fWaterDepth = gWaterDepth.Get();
	rGlobalLayout.fTerrainSunBrightness = std::max(0.25f, std::pow(fDayPercent, 0.25f));
	rGlobalLayout.fTerrainTwoPadZ = 0.0f;
	rGlobalLayout.fTerrainTwoPadW = 0.0f;

	// Normal flip is now per-island in TerrainNormal pass
	rGlobalLayout.fTerrainNormalXMultiplier = 1.0f;
	rGlobalLayout.fTerrainNormalYMultiplier = 1.0f;

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

	// Time of day
	rGlobalLayout.fLightingTimeOfDayMultiplier = std::min(fDayPercent + (1.0f - fDayPercent) * gLightingTimeOfDayMultiplier.Get(), 0.85f);
	rGlobalLayout.fLightingNightMultiplier = std::pow(fDayPercent, 0.5f);
	rGlobalLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - fDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();

	// Water global
	rGlobalLayout.fWaterTerrainHeight = gWaterTerrainHeight.Get();
	rGlobalLayout.fWaterTerrainFade = gWaterTerrainFade.Get();
	rGlobalLayout.fWaterNoiseFrequency = gWaterNoiseFrequency.Get();
	rGlobalLayout.fWaterNoiseAmount = gWaterNoiseAmount.Get();

	rGlobalLayout.fWaterDepthLutFeather = gWaterDepthLutFeather.Get();
	rGlobalLayout.fWaterDepthColorFeather = gWaterDepthColorFeather.Get();
	rGlobalLayout.fWaterDepthReflectionFeather = fDayPercent * gWaterDepthReflectionFeather.Get();
	rGlobalLayout.fWaterColorNoiseFrequency = gWaterColorNoiseFrequency.Get();
	
	rGlobalLayout.fWaterHighMultiplier = gHighMultiplier.Get();
	rGlobalLayout.fWaterHighScaleOne = gHighScaleOne.Get();
	rGlobalLayout.fWaterHighScaleTwo = gHighScaleTwo.Get();

	if (fSunAngle >= XM_PIDIV16 && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.fWaterSunVisibility = 1.0f - (fSunAngle - XM_PIDIV16) / (XM_PIDIV2 - XM_PIDIV16);
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI - XM_PIDIV16)
	{
		rGlobalLayout.fWaterSunVisibility = (fSunAngle - XM_PIDIV2) / (XM_PI - XM_PIDIV16 - XM_PIDIV2);
	}
	else
	{
		rGlobalLayout.fWaterSunVisibility = 1.0f;
	}
	rGlobalLayout.fWaterSunVisibility = std::pow(rGlobalLayout.fWaterSunVisibility, 2.0f);

	rGlobalLayout.fWaterFresnel = std::pow(fDayPercent, 0.5f) * gWaterFresnel.Get();
	rGlobalLayout.fWaterColorBottom = gWaterColorBottom.Get();
	rGlobalLayout.fWaterColorHeightInv = 1.0f / gWaterColorHeight.Get();
	rGlobalLayout.fWaterColorNoiseAmount = gWaterColorNoiseAmount.Get();

	if (fSunAngle >= 0.0f && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.fWaterDirectional = 1.0f - (fSunAngle) / XM_PIDIV2;
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI)
	{
		rGlobalLayout.fWaterDirectional = (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
	else
	{
		rGlobalLayout.fWaterDirectional = 1.0f;
	}
	rGlobalLayout.fWaterDirectional = std::pow(rGlobalLayout.fWaterDirectional, 2.0f);

	rGlobalLayout.fWaterFresnel2 = std::pow(fDayPercent, 0.5f) * gWaterFresnel2.Get();
	rGlobalLayout.fBeachDirectionalFadeBottom = gBeachDirectionalFadeBottom.Get();
	rGlobalLayout.fBeachDirectionalFadeHeightInv = 1.0f / gBeachDirectionalFadeHeight.Get();
	rGlobalLayout.fWaterLowSteepness = gLowSteepness.Get();

	rGlobalLayout.fWaterMediumSteepness = gMediumSteepness.Get();

	rGlobalLayout.iWaterLowCount = static_cast<int>(std::min(gLowCount.Get<int64_t>(), static_cast<int64_t>(gLowMax.Get())));
	rGlobalLayout.iWaterMediumCount = static_cast<int>(gMediumCount.Get<int64_t>());
}

void RenderFrameMain(int64_t iCommandBuffer, const std::unordered_map<GridCoord, game::FrameInterpolate>& rRenderInterpolates, const std::vector<GridCoord>& rActiveCoords, GridCoord cameraCoord, const std::unordered_map<GridCoord, std::unique_ptr<game::Frame>>& rCurrentFrames)
{
	const game::FrameInterpolate& rCameraInterpolate = rRenderInterpolates.at(cameraCoord);

	// One-time setup (preserved from original RenderFrameMain)
	RenderLightingMain(iCommandBuffer, rCameraInterpolate);
	gpBufferManager->ResetSkinningAllocations(iCommandBuffer);

	// Phase 1: BeginRender — compute total capacities, resize GPU buffers, reset counters
	game::FrameInterpolate::BeginRender(iCommandBuffer, rRenderInterpolates, rActiveCoords);

	// Phase 2: Render per-frame (camera first for index 0 stability)
	auto renderFrame = [&](const GridCoord& rCoord)
	{
		auto it = rRenderInterpolates.find(rCoord);
		if (it == rRenderInterpolates.end()) return;
		const game::FrameInterpolate& rInterp = it->second;
		uint16_t uiFrameId = rCurrentFrames.at(rCoord)->postRender.uiFrameId;
		// Main collections via Render (no uiFrameId needed)
		game::FrameInterpolate::Render(rInterp, iCommandBuffer);
		// SmokeTrails/WindTrails called separately with uiFrameId
		SmokeTrailsInterpolate::Render(rInterp, iCommandBuffer, uiFrameId);
		WindTrailsInterpolate::Render(rInterp, iCommandBuffer, uiFrameId);
	};
	renderFrame(cameraCoord);
	for (const GridCoord& rCoord : rActiveCoords)
	{
		if (rCoord == cameraCoord) continue;
		renderFrame(rCoord);
	}

	// Phase 3: EndRender — write indirect draw buffer counts
	game::FrameInterpolate::EndRender(iCommandBuffer);

	// Post-render MainLayout setup (camera matrices, wave params, hex shields, camera shake)
	const game::FrameInterpolate& rFrameInterpolate = rCameraInterpolate;
	shaders::MainLayout& rMainLayout = *reinterpret_cast<shaders::MainLayout*>(&gpBufferManager->mMainLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	static int siRenderCount = 0;
	rMainLayout.iFrameNumber = static_cast<int>(game::gpCamera->miFrame);
	rMainLayout.iRenderNumber = ++siRenderCount;

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
	gpProfileManager->SetCount(eCounter, iCount);

	int64_t iRendered = 0;
	for (int64_t i = 0; i < iCount; ++i)
	{
		auto& rVecPosition = pVecPositions[i];

		XMFLOAT4A f4Position {};
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
		rObjectLayout.uiColor = 0xFFFFFFFF;
		rObjectLayout.f4Position = f4Position;
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4Transform[0]), matTransform);
		XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(&rObjectLayout.f3x4TransformNormal[0]), XMMatrixTranspose(XMMatrixInverse(nullptr, matTransform)));

		++iRendered;
	}
	gpProfileManager->SetCount(eCounter + 1, iRendered);

	gpPipelineManager->mpPipelines[ePipeline].WriteIndirectBuffer(iCommandBuffer, iRendered);
	if (ePipelineShadow != kPipelineCount)
	{
		gpPipelineManager->mpPipelines[ePipelineShadow].WriteIndirectBuffer(iCommandBuffer, iRendered);
	}
}

static XMFLOAT4 sf4SmokeArea {};

void RenderSmokeGlobal(int64_t iCommandBuffer)
{
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fSmokeMax = gSmokeMax.Get();
	rGlobalLayout.fSmokePower = gSmokePower.Get();
	rGlobalLayout.fSmokeDecay = gSmokeDecay.Get();

	rGlobalLayout.fSmokeColorMin = gSmokeColorMin.Get();
	rGlobalLayout.fSmokeColorMultiplier = gSmokeColorMultiplier.Get();
	rGlobalLayout.fSmokeIntensityFalloff = gSmokeIntensityFalloff.Get();
	rGlobalLayout.fSmokeDecayExtra = gSmokeDecayExtra.Get();

	rGlobalLayout.fSmokeDecayExtraThreshold = gSmokeDecayExtraThreshold.Get();
	rGlobalLayout.fSmokeWindNoiseScale = gSmokeWindNoiseScale.Get();
	rGlobalLayout.fSmokeWindNoiseQuantity = gSmokeWindNoiseQuantity.Get();
	rGlobalLayout.fSmokeNoiseQuantity = gSmokeNoiseQuantity.Get();

	rGlobalLayout.fSmokeNoiseScaleOne = gSmokeNoiseScaleOne.Get();
	rGlobalLayout.fSmokeNoiseScaleTwo = gSmokeNoiseScaleTwo.Get();
	rGlobalLayout.fSmokeObjectHeightInv = 1.0f / gSmokeObjectHeight.Get();
	rGlobalLayout.fSmokeEdgeDecayDistanceInv = 1.0f / gSmokeEdgeDecayDistance.Get();
	rGlobalLayout.fSmokeNoiseInfluence = gSmokeNoiseInfluence.Get();

	static bool sbSmoke = false;
	if (sbSmoke != gSmoke.Get<bool>())
	{
		sbSmoke = gSmoke.Get<bool>();
		gbSmokeClear = true;
	}

	if (gbSmokeClear)
	{
		gbSmokeClear = false;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	static XMFLOAT4 sf4PreviousSmokeArea {};
	if (!gSmoke.Get<bool>())
	{
		rGlobalLayout.f4SmokeArea = sf4PreviousSmokeArea;

		gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	XMFLOAT4A f4PlayerPosition {};
	XMStoreFloat4A(&f4PlayerPosition, game::gpCamera->mVecPosition);
	float fAreaX = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	float fAreaY = 0.5f * (0.025f * 8000.0f * gSmokeSimulationArea.Get());
	rGlobalLayout.f4SmokeArea = {f4PlayerPosition.x - fAreaX, f4PlayerPosition.y + fAreaY, f4PlayerPosition.x + fAreaX, f4PlayerPosition.y - fAreaY};
	sf4SmokeArea = rGlobalLayout.f4SmokeArea;

	float fXOffset = (sf4PreviousSmokeArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousSmokeArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousSmokeArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousSmokeArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mSmokeSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Params = {};
	sf4PreviousSmokeArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineSmokeClearA].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeClearB].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadB].WriteIndirectBuffer(iCommandBuffer, 1);
	gpPipelineManager->mpPipelines[kPipelineSmokeSpreadA].WriteIndirectBuffer(iCommandBuffer, 1);
}

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

	if (gbWindClear)
	{
		gbWindClear = false;

		gpPipelineManager->mpPipelines[kPipelineWindClearA].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineWindClearB].WriteIndirectBuffer(iCommandBuffer, 1);
		gpPipelineManager->mpPipelines[kPipelineWindSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineWindSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
	}

	if (!gWind.Get<bool>())
	{
		gpPipelineManager->mpPipelines[kPipelineWindClearA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineWindClearB].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineWindSpreadA].WriteIndirectBuffer(iCommandBuffer, 0);
		gpPipelineManager->mpPipelines[kPipelineWindSpreadB].WriteIndirectBuffer(iCommandBuffer, 0);

		return;
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
	rGlobalLayout.fWindTexelSize = 1.0f / static_cast<float>(gpTextureManager->mWindTextureOne.mInfo.extent.width);
	rGlobalLayout.fWindTime = sfWindTime;
	rGlobalLayout.fWindSmokeRetention = gWindSmokeRetention.Get();
	rGlobalLayout.fWindToSmokePower = gWindToSmokePower.Get();
	rGlobalLayout.fWindDiffusionHigh = gWindDiffusionHigh.Get();
	rGlobalLayout.fWindDiffusionLow = gWindDiffusionLow.Get();

	rGlobalLayout.fWindDisplacementNoiseScale = gWindDisplacementNoiseScale.Get();
	rGlobalLayout.fWindDisplacementSwirlScale = gWindDisplacementSwirlScale.Get();
	rGlobalLayout.fWindDisplacementSwirlPower = gWindDisplacementSwirlPower.Get();

	// Toggle ping-pong index
	giWindTextureIndex = 1 - giWindTextureIndex;

	rGlobalLayout.fWindTextureIndex = static_cast<float>(giWindTextureIndex);

	// Compute wind spread quad offset (shares smoke area coordinate space)
	static XMFLOAT4 sf4PreviousWindArea {};
	float fXOffset = (sf4PreviousWindArea.x - rGlobalLayout.f4SmokeArea.x) / (sf4PreviousWindArea.z - rGlobalLayout.f4SmokeArea.x);
	float fYOffset = (sf4PreviousWindArea.y - rGlobalLayout.f4SmokeArea.y) / (sf4PreviousWindArea.w - rGlobalLayout.f4SmokeArea.y);
	shaders::AxisAlignedQuadLayout& rQuad = *reinterpret_cast<shaders::AxisAlignedQuadLayout*>(gpBufferManager->mWindSpreadStorageBuffers.at(iCommandBuffer).mpMappedMemory);
	rQuad.f4VertexRect = {-1.0f + 2.0f * fXOffset, 1.0f - 2.0f * fYOffset, 2.0f, -2.0f};
	rQuad.f4TextureRect = {0.0f, 0.0f, 1.0f, 1.0f};
	rQuad.f4Params = {};
	sf4PreviousWindArea = rGlobalLayout.f4SmokeArea;

	gpPipelineManager->mpPipelines[kPipelineWindClearA].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineWindClearB].WriteIndirectBuffer(iCommandBuffer, 0);
	gpPipelineManager->mpPipelines[kPipelineWindSpreadA].WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 0 ? 1 : 0);
	gpPipelineManager->mpPipelines[kPipelineWindSpreadB].WriteIndirectBuffer(iCommandBuffer, giWindTextureIndex == 1 ? 1 : 0);
}

} // namespace engine

#endif // BT_CLIENT
