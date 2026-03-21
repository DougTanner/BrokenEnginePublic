#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

static void PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float& rfDayPercent, float& rfNoonPercent)
{
	// Sun
	XMVECTOR vecSunNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	XMMATRIX matSunRotation = XMMatrixRotationY(-fSunAngle);
	vecSunNormal = XMVector4Normalize(XMVector4Transform(vecSunNormal, matSunRotation));
	XMStoreFloat4(&rGlobalLayout.f4SunNormal, vecSunNormal);

	// Sunlight
	float fAmbientNight = std::max(kfDefaultMinimumAmbient, gMinimumAmbient.Get());
	float fAmbientMorning = std::max(0.075f, gMinimumAmbient.Get());
	XMVECTOR vecSunMorning = 0.5f * XMVectorSet(1.0f, 219.0f / 255.0f, 0.0f, 1.0f);
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
		vecSun = vecSunNoon;
		vecAmbient = vecAmbientNoon;
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
	rfNoonPercent = 0.0f;
	if (fSunAngle >= kfNoonFeatherEnd && fSunAngle <= XM_PIDIV2)
	{
		rfNoonPercent = (fSunAngle - kfNoonFeatherEnd) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}
	else if (fSunAngle > XM_PIDIV2 && fSunAngle <= (XM_PI - kfNoonFeatherEnd))
	{
		rfNoonPercent = 1.0f - (fSunAngle - XM_PIDIV2) / (XM_PIDIV2 - kfNoonFeatherEnd);
	}

	rfDayPercent = 0.0f;
	if (fSunAngle >= 0.0f && fSunAngle <= XM_PIDIV2)
	{
		rfDayPercent = fSunAngle / XM_PIDIV2;
	}
	else if (fSunAngle > XM_PIDIV2 && fSunAngle <= XM_PI)
	{
		rfDayPercent = 1.0f - (fSunAngle - XM_PIDIV2) / XM_PIDIV2;
	}
}

static void PopulateShadowParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent, float fNoonPercent)
{
	// Shadow texture
	float fShadowTextureSizeWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width);
	float fShadowTextureSizeHeight = static_cast<float>(gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height);
	float fShadowElevationTextureSizeWidth = fShadowTextureSizeWidth + 0.5f * fShadowTextureSizeWidth;

	float fShadowNoon = std::pow(fDayPercent, gShadowFeatherPower.Get());
	float fShadowEvening = 1.0f - fShadowNoon;
	float fOffsetNoon = std::pow(fNoonPercent, 2.0f);

	rGlobalLayout.fShadowFeather = 1.0f / (fShadowNoon * gShadowFeatherNoon.Get() + fShadowEvening * gShadowFeatherSunset.Get());
	rGlobalLayout.fShadowNoonOffset = fOffsetNoon * gShadowFeatherNoonOffset.Get();
	rGlobalLayout.fShadowDistanceFalloff = gShadowDistanceFallof.Get();
	rGlobalLayout.fShadowBlurSigma = gShadowBlurSigma.Get();

	rGlobalLayout.fObjectShadowsBlurDistance = fDayPercent * gObjectShadowsBlurDistanceNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsBlurDistanceSunset.Get();
	rGlobalLayout.fObjectShadowsBlurSigma = gObjectShadowsBlurSigma.Get();
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
	if (fSunAngle >= kfSunriseStretchBegin || fSunAngle < 0.0f)
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

	rGlobalLayout.iShadowTextureWidth = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.iShadowTextureHeight = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent.height; // Y pixels
	rGlobalLayout.iObjectShadowTextureWidth = gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.width; // X pixels
	rGlobalLayout.iObjectShadowTextureHeight = gpTextureManager->mRenderTargetTextures.mObjectShadowsTexture.mInfo.extent.height; // Y pixels

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
}

static void PopulateTerrainParameters(shaders::GlobalLayout& rGlobalLayout, float fDayPercent, float fNoonPercent)
{
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
}

static void PopulateWaterParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent)
{
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

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime, int64_t iTick)
{
	RenderLightingGlobal(iCommandBuffer);
	RenderSmokeGlobal(iCommandBuffer);
	RenderWindGlobal(iCommandBuffer);

	float fSunAngle = game::gpCamera->SunAngle();

	// Global data
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.iCommandBuffer = static_cast<int>(iCommandBuffer);
	rGlobalLayout.iCameraFrame = static_cast<int>(game::gpCamera->miFrame);
	rGlobalLayout.iTickCounter = static_cast<int>(iTick);
	rGlobalLayout.iCommandBufferPad = static_cast<int>(iCommandBuffer);

	rGlobalLayout.fElapsedTime = fCurrentTime;
	rGlobalLayout.fBaseHeight = gBaseHeight.Get();
	rGlobalLayout.fAspectRatio = gpSwapchainManager->mfAspectRatio;
	rGlobalLayout.fDetailTextureAspectRatio = TextureManager::DetailTextureAspectRatio();

	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	// Lighting area: stable dimensions + texel-snapped origin
	float fVisibleWidth = rGlobalLayout.f4VisibleArea.z - rGlobalLayout.f4VisibleArea.x;
	float fVisibleHeight = rGlobalLayout.f4VisibleArea.y - rGlobalLayout.f4VisibleArea.w;
	if (fVisibleWidth > 0.0f && fVisibleHeight > 0.0f)
	{
		auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingTextureMultiplier.Get());

		float fWidth = std::ceil(fVisibleWidth);
		float fHeight = std::ceil(fVisibleHeight);

		float fTexelSizeX = fWidth / static_cast<float>(iLightingTextureX);
		float fTexelSizeY = fHeight / static_cast<float>(iLightingTextureY);

		XMFLOAT4A f4CameraPos {};
		XMStoreFloat4A(&f4CameraPos, game::gpCamera->mVecPosition);

		float fLeft = common::RoundDown(f4CameraPos.x - fWidth * 0.5f, fTexelSizeX);
		float fTop = common::RoundDown(f4CameraPos.y + fHeight * 0.5f + fTexelSizeY, fTexelSizeY);

		rGlobalLayout.f4LightingArea = {fLeft, fTop, fLeft + fWidth, fTop - fHeight};
	}
	else
	{
		rGlobalLayout.f4LightingArea = rGlobalLayout.f4VisibleArea;
	}

	float fDayPercent = 0.0f;
	float fNoonPercent = 0.0f;
	PopulateSunAndLighting(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent);
	PopulateShadowParameters(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent);
	PopulateTerrainParameters(rGlobalLayout, fDayPercent, fNoonPercent);
	PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent);
}

} // namespace engine

#endif // BT_CLIENT
