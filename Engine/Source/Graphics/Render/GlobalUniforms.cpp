#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"

namespace engine
{

// 0.0 throughout day, 1.0 throughout night, smooth in narrow sunrise/sunset windows.
// Single source of truth for the night-gate envelope used by the sun/moon split, the shadow
// moon multiplier, and the water moon-brightness lerp.
static float ComputeNightAmount(float fSunAngle)
{
	const float fSunsetStart = gSunMoonShadowSunsetStart.Get();
	const float fSunsetEnd = gSunMoonShadowSunsetEnd.Get();
	const float fSunriseStart = gSunMoonShadowSunriseStart.Get();
	const float fSunriseEnd = gSunMoonShadowSunriseEnd.Get();
	if (fSunAngle >= fSunsetStart && fSunAngle <= fSunsetEnd)
	{
		return (fSunAngle - fSunsetStart) / (fSunsetEnd - fSunsetStart);
	}
	if (fSunAngle > fSunsetEnd || fSunAngle <= fSunriseStart)
	{
		return 1.0f;
	}
	if (fSunAngle >= fSunriseStart && fSunAngle <= fSunriseEnd)
	{
		return 1.0f - (fSunAngle - fSunriseStart) / (fSunriseEnd - fSunriseStart);
	}
	return 0.0f;
}

static void PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float& rfDayPercent, float& rfNoonPercent)
{
	// Sun/Moon direction: night reverses across the sky from sunset back to sunrise
	float fDirectionAngle = fSunAngle < XM_PI ? fSunAngle : XM_2PI - fSunAngle;
	XMVECTOR vecSunMoonNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	XMMATRIX matSunRotation = XMMatrixRotationY(-fDirectionAngle);
	vecSunMoonNormal = XMVector4Normalize(XMVector4Transform(vecSunMoonNormal, matSunRotation));
	XMStoreFloat4(&rGlobalLayout.f4SunMoonNormal, vecSunMoonNormal);
	rGlobalLayout.f4SunMoonNormal.w = fSunAngle;

	// Sun/Moon color
	float fAmbientNight = gMinimumAmbient.Get();
	float fAmbientMorning = std::max(0.075f, gMinimumAmbient.Get());
	XMVECTOR vecSunMorning = 0.5f * XMVectorSet(1.0f, 219.0f / 255.0f, 0.0f, 1.0f);
	XMVECTOR vecAmbientMorning = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecSunNoon = XMVectorSet(0.8f, 0.8f, 0.8f, 1.0f);
	XMVECTOR vecAmbientNoon = XMVectorSet(0.2f, 0.2f, 0.2f, 1.0f);
	XMVECTOR vecSunEvening = 0.75f * XMVectorSet(0.8f, 0.4f, 0.4f, 1.0f);
	XMVECTOR vecAmbientEvening = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecMidnight = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR vecAmbientMidnight = XMVectorSet(fAmbientNight, fAmbientNight, fAmbientNight, 1.0f);
	float fMoonBrightness = gMoonBrightness.Get();
	XMVECTOR vecMoonFloor = XMVectorSet(fMoonBrightness, fMoonBrightness, fMoonBrightness * gSunMoonMoonBlueTint.Get(), 0.0f);

	XMVECTOR vecSunMoon = vecMidnight;
	XMVECTOR vecAmbient = vecAmbientMidnight;

	const float fMorning = gSunMoonMorning.Get();
	const float fNoonStart = gSunMoonNoonStart.Get();
	const float fNoonEnd = gSunMoonNoonEnd.Get();
	const float fEvening = gSunMoonEvening.Get();
	const float fNightStart = gSunMoonNightStart.Get();

	if (fSunAngle >= fMorning && fSunAngle < fNoonStart)
	{
		float fLerp = (fSunAngle - fMorning) / (fNoonStart - fMorning);
		vecSunMoon = XMVectorLerp(vecSunMorning, vecSunNoon, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMorning, vecAmbientNoon, fLerp);
	}
	else if (fSunAngle >= fNoonStart && fSunAngle < fNoonEnd)
	{
		vecSunMoon = vecSunNoon;
		vecAmbient = vecAmbientNoon;
	}
	else if (fSunAngle >= fNoonEnd && fSunAngle < fEvening)
	{
		float fLerp = (fSunAngle - fNoonEnd) / (fEvening - fNoonEnd);
		vecSunMoon = XMVectorLerp(vecSunNoon, vecSunEvening, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientNoon, vecAmbientEvening, fLerp);
	}
	else if (fSunAngle >= fEvening && fSunAngle < fNightStart)
	{
		float fLerp = (fSunAngle - fEvening) / (fNightStart - fEvening);
		vecSunMoon = XMVectorLerp(vecSunEvening, vecMidnight, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientEvening, vecAmbientMidnight, fLerp);
	}
	else if (fSunAngle >= fNightStart)
	{
		vecAmbient = vecAmbientMidnight;
	}
	else if (fSunAngle >= 0.0f)
	{
		float fLerp = fSunAngle / fMorning;
		vecSunMoon = XMVectorLerp(vecMidnight, vecSunMorning, fLerp);
		vecAmbient = XMVectorLerp(vecAmbientMidnight, vecAmbientMorning, fLerp);
	}
	else
	{
		DEBUG_BREAK();
	}

	// Sun: piecewise lerp goes naturally to (0,0,0) at midnight; no moon-floor clamp here.
	XMVECTOR vecSun = vecSunMoon * gSunMoonSunIntensity.Get();
	XMStoreFloat4(&rGlobalLayout.f4SunColor, vecSun);

	// Moon: floor color modulated by night-amount envelope and intensity slider.
	float fNightAmount = ComputeNightAmount(fSunAngle);
	XMVECTOR vecMoon = vecMoonFloor * (fNightAmount * gSunMoonMoonIntensity.Get());
	XMStoreFloat4(&rGlobalLayout.f4MoonColor, vecMoon);

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
	rGlobalLayout.fWaterReducedNoiseOriginX = 0.0f;

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
			rGlobalLayout.fShadowSunAngle = XM_2PI - fSunAngle;
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
		rGlobalLayout.fShadowSunAngle = fSunAngle >= XM_PI ? fSunAngle - XM_PI : XM_PI - fSunAngle;
		rGlobalLayout.fShadowDirectionMultiplier = -1.0f;

		rGlobalLayout.iShadowElevationSize = 0; // !=
		rGlobalLayout.iShadowIncrement = -1; // ++
		rGlobalLayout.iShadowStartOffset = static_cast<int>(fShadowTextureSizeWidth / 2.0f); // Start offset
	}

	// Shadow multiplier: 1.0 during day, gSunMoonShadowNightMultiplier at night.
	// Derived from the shared night-amount envelope so the sun/moon split and the shadow
	// night-gate stay in lockstep.
	rGlobalLayout.fShadowMoonMultiplier = std::lerp(1.0f, gSunMoonShadowNightMultiplier.Get(), ComputeNightAmount(fSunAngle));
}

static void PopulateTerrainParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent, float fNoonPercent)
{
	// Terrain
	rGlobalLayout.fIslandHeight = gIslandHeight.Get();
	rGlobalLayout.fIslandAmbientOcclusion = fNoonPercent * gIslandAmbientOcclusion.Get();
	rGlobalLayout.fTerrainEarlyOut = gTerrainEarlyOut.Get();
	rGlobalLayout.fWaterEarlyOut = gWaterEarlyOut.Get();

	rGlobalLayout.fWaterDepth = gWaterDepth.Get();
	rGlobalLayout.fTerrainSunBrightness = std::max(0.25f, std::pow(fDayPercent, 0.25f));
	rGlobalLayout.fWaterReducedNormalOriginX = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginY = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginTwoX = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginTwoY = 0.0f;

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
	rGlobalLayout.fLightingTimeOfDayMultiplier = fDayPercent * gLightingDayFinalMultiplier.Get() + (1.0f - fDayPercent) * gLightingNightFinalMultiplier.Get();
	rGlobalLayout.fLightingNightMultiplier = std::pow(fDayPercent, 0.5f);
	// Water moon brightness gates on the same night-amount envelope as the sun/moon split and shadow night-gate.
	rGlobalLayout.fLightingWaterMoonBrightness = std::lerp(1.0f, gLightingWaterMoonBrightness.Get(), ComputeNightAmount(fSunAngle));
	rGlobalLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - fDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();
}

static void PopulateWaterParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent)
{
	// Water global
	rGlobalLayout.fWaterHeight = gWaterHeight.Get();
	rGlobalLayout.fWaterTerrainHeight = gWaterTerrainHeight.Get();
	rGlobalLayout.fWaterTerrainFade = gWaterTerrainFade.Get();
	rGlobalLayout.fWaterTerrainFadeClamp = gWaterTerrainFadeClamp.Get();
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
	rGlobalLayout.fBeachFadeTop = gBeachFadeTop.Get();
	rGlobalLayout.fBeachFadeInvRange = 1.0f / (gBeachFadeBottom.Get() - gBeachFadeTop.Get());
	rGlobalLayout.fWaterLowSteepness = gLowSteepness.Get();

	rGlobalLayout.fWaterMediumSteepness = gMediumSteepness.Get();

	rGlobalLayout.iWaterLowCount = static_cast<int>(std::min(gLowCount.Get<int64_t>(), static_cast<int64_t>(gLowMax.Get())));
	rGlobalLayout.iWaterMediumCount = static_cast<int>(gMediumCount.Get<int64_t>());

	// Water precision: camera-relative UV reduction (double precision on CPU)
	// Normal map mod uses 10.0 (not 1.0) because the shader multiplies reducedOrigin by non-integer
	// sizeMult values (0.2, 1.1, 2.5, etc.). With mod 1.0, wraps produce non-integer UV jumps that
	// fract() can't absorb. With mod 10.0, sizeMult * 10 is always an integer for current multipliers.
	XMFLOAT4A f4CameraPos {};
	XMStoreFloat4A(&f4CameraPos, game::gpCamera->mVecPosition);
	rGlobalLayout.fWaterOriginX = f4CameraPos.x;
	rGlobalLayout.fWaterOriginY = f4CameraPos.y;

	double dSizeBaseOne = static_cast<double>(gLightingSampledNormalsOneSize.Get());
	double dSizeBaseTwo = static_cast<double>(gLightingSampledNormalsTwoSize.Get());
	double dSizeBaseThree = static_cast<double>(gLightingSampledNormalsThreeSize.Get());
	double dSpeed = static_cast<double>(gLightingSampledNormalsSpeed.Get());
	double dTime = static_cast<double>(rGlobalLayout.fElapsedTime);
	double dCameraX = static_cast<double>(f4CameraPos.x);
	double dCameraY = static_cast<double>(f4CameraPos.y);

	rGlobalLayout.fWaterReducedNormalOriginX = static_cast<float>(std::fmod(dSizeBaseOne * dCameraX, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginY = static_cast<float>(std::fmod(dSizeBaseOne * dCameraY, 10.0));
	rGlobalLayout.fWaterReducedNormalTime = static_cast<float>(std::fmod(dSizeBaseOne * dSpeed * dTime, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginTwoX = static_cast<float>(std::fmod(dSizeBaseTwo * dCameraX, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginTwoY = static_cast<float>(std::fmod(dSizeBaseTwo * dCameraY, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeTwo = static_cast<float>(std::fmod(dSizeBaseTwo * dSpeed * dTime, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginThreeX = static_cast<float>(std::fmod(dSizeBaseThree * dCameraX, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginThreeY = static_cast<float>(std::fmod(dSizeBaseThree * dCameraY, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeThree = static_cast<float>(std::fmod(dSizeBaseThree * dSpeed * dTime, 10.0));

	double dNoiseFreq = static_cast<double>(gWaterColorNoiseFrequency.Get());
	rGlobalLayout.fWaterReducedNoiseOriginX = static_cast<float>(std::fmod(dNoiseFreq * dCameraX, 1.0));
	rGlobalLayout.fWaterReducedNoiseOriginY = static_cast<float>(std::fmod(dNoiseFreq * dCameraY, 1.0));
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

	rGlobalLayout.fElapsedTime = fCurrentTime;
	rGlobalLayout.fBaseHeight = gBaseHeight.Get();
	rGlobalLayout.fAspectRatio = gpSwapchainManager->mfAspectRatio;
	rGlobalLayout.fDetailTextureAspectRatio = TextureManager::DetailTextureAspectRatio();

	XMFLOAT4A f4CameraPosGlobal {};
	XMStoreFloat4A(&f4CameraPosGlobal, game::gpCamera->mVecPosition);
	rGlobalLayout.f2CameraPosition.x = f4CameraPosGlobal.x;
	rGlobalLayout.f2CameraPosition.y = f4CameraPosGlobal.y;
	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	// Lighting area: fixed world-space dimensions, origin shifts one texel at a time
	float fVisibleWidth = rGlobalLayout.f4VisibleArea.z - rGlobalLayout.f4VisibleArea.x;
	float fVisibleHeight = rGlobalLayout.f4VisibleArea.y - rGlobalLayout.f4VisibleArea.w;
	if (fVisibleWidth > 0.0f && fVisibleHeight > 0.0f)
	{
		auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingDepositTextureMultiplier.Get());
		float fTexelsX = static_cast<float>(iLightingTextureX);
		float fTexelsY = static_cast<float>(iLightingTextureY);

		// Texel size derived from deposit texture pixels and visible area
		float fTexelSizeX = fVisibleWidth / fTexelsX;
		float fTexelSizeY = fVisibleHeight / fTexelsY;

		// Fixed dimensions: exactly texturePixels * texelSize
		float fWidth = fTexelSizeX * fTexelsX;
		float fHeight = fTexelSizeY * fTexelsY;

		XMFLOAT4A f4CameraPos {};
		XMStoreFloat4A(&f4CameraPos, game::gpCamera->mVecPosition);

		// Integer texel math: compute origin as integer texel index * texelSize
		int64_t iLeftTexel = static_cast<int64_t>(std::floor((f4CameraPos.x - fWidth * 0.5f) / fTexelSizeX));
		int64_t iTopTexel = static_cast<int64_t>(std::floor((f4CameraPos.y + fHeight * 0.5f) / fTexelSizeY));
		float fLeft = static_cast<float>(iLeftTexel) * fTexelSizeX;
		float fTop = static_cast<float>(iTopTexel) * fTexelSizeY;

		rGlobalLayout.f4LightingArea = {fLeft, fTop, fLeft + fWidth, fTop - fHeight};

		rGlobalLayout.uiLightTilesX = std::max(1u, static_cast<uint32_t>(iLightingTextureX) / shaders::kiComputeTileSize);
		rGlobalLayout.uiLightTilesY = std::max(1u, static_cast<uint32_t>(iLightingTextureY) / shaders::kiComputeTileSize);
	}
	else
	{
		rGlobalLayout.f4LightingArea = rGlobalLayout.f4VisibleArea;
	}

	float fDayPercent = 0.0f;
	float fNoonPercent = 0.0f;
	PopulateSunAndLighting(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent);
	PopulateShadowParameters(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent);
	PopulateTerrainParameters(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent);
	PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent);

	// Debug
	rGlobalLayout.fDebugTextureIndex = gDebugTextureIndex.Get();
	rGlobalLayout.fDebugTextureFormat = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpDebugTextureFormats[static_cast<int64_t>(gDebugTextureIndex.Get())]);
	rGlobalLayout.fDebugTextureLinearRange = gDebugTextureLinearRange.Get();

	// DT: TEMP - capture sun/moon snapshot for HUD debug overlay.
	gDebugSunMoonAngle = fSunAngle;
	gDebugSunMoonNormal = rGlobalLayout.f4SunMoonNormal;
	gDebugSunColor = rGlobalLayout.f4SunColor;
	gDebugMoonColor = rGlobalLayout.f4MoonColor;
	gDebugAmbientColor = rGlobalLayout.f4AmbientColor;
	gDebugSunMoonDayPercent = fDayPercent;
	gDebugSunMoonNoonPercent = fNoonPercent;
	gDebugSunMoonShadowMoonMultiplier = rGlobalLayout.fShadowMoonMultiplier;
	gDebugSunMoonLightingWaterMoonBrightness = rGlobalLayout.fLightingWaterMoonBrightness;
	gDebugSunMoonWaterSunVisibility = rGlobalLayout.fWaterSunVisibility;
	gDebugSunMoonWaterDirectional = rGlobalLayout.fWaterDirectional;
	gDebugSunMoonTerrainSunBrightness = rGlobalLayout.fTerrainSunBrightness;
}

} // namespace engine

#endif // defined(BT_CLIENT)
