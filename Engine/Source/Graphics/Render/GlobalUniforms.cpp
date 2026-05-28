#if defined(BT_CLIENT)

#include "Render.h"

#include "Game.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/SmokeWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"
#include "Ui/TerrainWrappersBase.h"
#include "Ui/WaterWrappersBase.h"
#include "Ui/WindWrappersBase.h"

namespace engine
{

// 0.0 outside the rise/set window (day side), 1.0 inside (night side), smooth lerps in the
// rise and set transition windows. Shape parameters are 4 sun-angle boundaries: rise opens at
// fRiseStart and finishes at fRiseEnd (transition from day to night side), set opens at
// fSetStart and finishes at fSetEnd (transition back).
static float ComputeNightEnvelope(float fSunAngle, float fRiseStart, float fRiseEnd, float fSetStart, float fSetEnd)
{
	if (fSunAngle >= fRiseStart && fSunAngle <= fRiseEnd)
	{
		return (fSunAngle - fRiseStart) / (fRiseEnd - fRiseStart);
	}
	if (fSunAngle > fRiseEnd || fSunAngle <= fSetStart)
	{
		return 1.0f;
	}
	if (fSunAngle >= fSetStart && fSunAngle <= fSetEnd)
	{
		return 1.0f - (fSunAngle - fSetStart) / (fSetEnd - fSetStart);
	}
	return 0.0f;
}

// Shadow night-gate envelope — drives fShadowMoonMultiplier (Shadow.comp output scaling).
static float ComputeNightAmount(float fSunAngle)
{
	return ComputeNightEnvelope(fSunAngle, gSunMoonShadowSunsetStart.Get(), gSunMoonShadowSunsetEnd.Get(), gSunMoonShadowSunriseStart.Get(), gSunMoonShadowSunriseEnd.Get());
}

// Moon-color envelope — drives the moon-floor scaling in PopulateSunAndLighting. Independent
// from the shadow night-gate so the moon can rise/set on its own schedule.
static float ComputeMoonAmount(float fSunAngle)
{
	return ComputeNightEnvelope(fSunAngle, gSunMoonMoonriseStart.Get(), gSunMoonMoonriseEnd.Get(), gSunMoonMoonsetStart.Get(), gSunMoonMoonsetEnd.Get());
}

static void PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float& rfDayPercent, float& rfNoonPercent)
{
	// Sun/Moon direction: night reverses across the sky from sunset back to sunrise
	float fDirectionAngle = fSunAngle < XM_PI ? fSunAngle : XM_2PI - fSunAngle;
	XMVECTOR vecSunMoonNormal = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
	XMMATRIX matSunRotation = XMMatrixRotationY(-fDirectionAngle);
	vecSunMoonNormal = XMVector4Transform(vecSunMoonNormal, matSunRotation);
	// Normal-only pitch tilt around world X. Shadows derive from raw fSunAngle and are unaffected.
	XMMATRIX matSunTilt = XMMatrixRotationX(gSunMoonNormalTilt.Get());
	vecSunMoonNormal = XMVector4Normalize(XMVector4Transform(vecSunMoonNormal, matSunTilt));
	XMStoreFloat4(&rGlobalLayout.f4SunMoonNormal, vecSunMoonNormal);
	rGlobalLayout.f4SunMoonNormal.w = fSunAngle;

	// Sun/Moon color
	float fAmbientNight = gSunMoonMinimumAmbient.Get();
	float fAmbientMorning = std::max(0.075f, gSunMoonMinimumAmbient.Get());
	XMVECTOR vecSunMorning = XMVectorScale(XMVectorSet(1.0f, 219.0f / 255.0f, 0.0f, 1.0f), 0.5f);
	XMVECTOR vecAmbientMorning = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecSunNoon = XMVectorSet(0.8f, 0.8f, 0.8f, 1.0f);
	XMVECTOR vecAmbientNoon = XMVectorSet(0.2f, 0.2f, 0.2f, 1.0f);
	XMVECTOR vecSunEvening = XMVectorScale(XMVectorSet(0.8f, 0.4f, 0.4f, 1.0f), 0.75f);
	XMVECTOR vecAmbientEvening = XMVectorSet(fAmbientMorning, fAmbientMorning, fAmbientMorning, 1.0f);
	XMVECTOR vecMidnight = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR vecAmbientMidnight = XMVectorSet(fAmbientNight, fAmbientNight, fAmbientNight, 1.0f);
	// Moon floor color is unit-magnitude; per-target moon intensity sliders scale at shader read sites.
	XMVECTOR vecMoonFloor = XMVectorSet(1.0f, 1.0f, gSunMoonMoonBlueTint.Get(), 0.0f);

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
	// Per-target sun intensity sliders scale at shader read sites, not here.
	XMStoreFloat4(&rGlobalLayout.f4SunColor, vecSunMoon);

	// Moon: floor color modulated by the moonrise/moonset envelope. Per-target moon intensity
	// sliders scale at shader read sites, not here.
	float fMoonAmount = ComputeMoonAmount(fSunAngle);
	XMVECTOR vecMoon = XMVectorScale(vecMoonFloor, fMoonAmount);
	XMStoreFloat4(&rGlobalLayout.f4MoonColor, vecMoon);

	rGlobalLayout.fSunIntensityTerrain  = gSunMoonSunIntensityTerrain.Get();
	rGlobalLayout.fSunIntensityWater    = gSunMoonSunIntensityWater.Get();
	rGlobalLayout.fSunIntensityObjects  = gSunMoonSunIntensityObjects.Get();
	rGlobalLayout.fSunIntensitySmoke    = gSunMoonSunIntensitySmoke.Get();
	rGlobalLayout.fMoonIntensityTerrain = gSunMoonMoonIntensityTerrain.Get();
	rGlobalLayout.fMoonIntensityWater   = gSunMoonMoonIntensityWater.Get();
	rGlobalLayout.fMoonIntensityObjects = gSunMoonMoonIntensityObjects.Get();
	rGlobalLayout.fMoonIntensitySmoke   = gSunMoonMoonIntensitySmoke.Get();

	vecAmbient = XMVectorSetW(XMVectorScale(vecAmbient, gSunMoonAmbientMultiplier.Get()), 1.0f);
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
	rGlobalLayout.fShadowDistanceFalloff = gShadowDistanceFalloff.Get();
	rGlobalLayout.fShadowBlurSigma = gShadowBlurSigma.Get();

	rGlobalLayout.fObjectShadowsBlurSigma = gObjectShadowsBlurSigma.Get();
	rGlobalLayout.iObjectShadowsBlurRadius = static_cast<int32_t>(gObjectShadowsBlurRadius.Get());
	rGlobalLayout.fObjectShadowsGrow = gObjectShadowsGrow.Get();
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

	// Shadow area: ramped-world-size texels. The texture is pre-sized (RenderTargetTextures) to cover the visible
	// area at Camera::kfEyeHeightMaxReference. The texel world size is the reference texel scaled by the camera's
	// rate-limited mfShadowTexelEyeHeight / kfCameraEyeHeightDefault, so it stays fixed at a settled height (the
	// grid snaps cleanly under XY pan -> no shimmer) and only rescales while the ramp tracks a zoom. At the default
	// height the scale is 1.0 (the reference texel), giving a centered sub-window of texW/4; the ramp returns the
	// window toward that count when zoomed out (recovering ray-march cost) rather than ray-marching the full texture.
	// f4ShadowArea is the full footprint, camera-centered and snapped to the current texel grid (integer-texel pan);
	// farther-than-window content crops via the CLAMP sampler. The base texel derives from the analytic straight-down
	// frustum width (gFov/aspect), never the snapped render-area width, so it stays bit-stable at a settled height.
	float fAspect = gpSwapchainManager->mfAspectRatio;
	float fTanHalfFov = std::tan(0.5f * XMConvertToRadians(gFov.Get() / fAspect));
	float fTexelScale = game::gpCamera->mfShadowTexelEyeHeight / game::Camera::kfCameraEyeHeightDefault;
	float fWorldTexelX = ((2.0f * game::Camera::kfEyeHeightMaxReference * fAspect * fTanHalfFov) / fShadowTextureSizeWidth) * fTexelScale;
	float fWorldTexelY = ((2.0f * game::Camera::kfEyeHeightMaxReference * fTanHalfFov) / fShadowTextureSizeHeight) * fTexelScale;
	float fFullWidth = fShadowTextureSizeWidth * fWorldTexelX;
	float fFullHeight = fShadowTextureSizeHeight * fWorldTexelY;
	XMFLOAT4A f4CameraPosition {};
	XMStoreFloat4A(&f4CameraPosition, game::gpCamera->mVecPosition);
	int64_t iLeftTexel = static_cast<int64_t>(std::floor((f4CameraPosition.x - fFullWidth * 0.5f) / fWorldTexelX));
	int64_t iTopTexel = static_cast<int64_t>(std::floor((f4CameraPosition.y + fFullHeight * 0.5f) / fWorldTexelY));
	float fLeft = static_cast<float>(iLeftTexel) * fWorldTexelX;
	float fTop = static_cast<float>(iTopTexel) * fWorldTexelY;
	rGlobalLayout.f4ShadowArea = {fLeft, fTop, fLeft + fFullWidth, fTop - fFullHeight};

	// Temporal accumulation: feed the previous frame's shadow area so ShadowTemporal.comp can reproject the
	// history into the current grid (mirrors the smoke/wind previous-area latch). First frame: previous ==
	// current and blend forced to 1.0 (pure current) so the uninitialized history texture is never shown; that
	// frame's copy seeds valid history. Once-per-frame latch (RenderFrameGlobal runs once per frame).
	static bool sbPreviousShadowAreaInitialized = false;
	static XMFLOAT4 sf4PreviousShadowArea {};
	if (!sbPreviousShadowAreaInitialized)
	{
		sf4PreviousShadowArea = rGlobalLayout.f4ShadowArea;
		sbPreviousShadowAreaInitialized = true;
		rGlobalLayout.fShadowTemporalBlend = 1.0f;
	}
	else
	{
		rGlobalLayout.fShadowTemporalBlend = gShadowTemporalBlend.Get();
	}
	rGlobalLayout.f4ShadowAreaPrevious = sf4PreviousShadowArea;
	sf4PreviousShadowArea = rGlobalLayout.f4ShadowArea;

	// Centered visible window (texels) from the live eye height — only this region is ray-marched. min() against the
	// texture extent both crops above the reference height and caps the transient when a fast zoom-out outruns the ramp.
	float fVisibleWidthNow = 2.0f * game::gpCamera->mfCameraEyeHeight * fAspect * fTanHalfFov;
	float fVisibleHeightNow = 2.0f * game::gpCamera->mfCameraEyeHeight * fTanHalfFov;
	int iShadowSubWidth = std::min(static_cast<int>(std::lround(fVisibleWidthNow / fWorldTexelX)), static_cast<int>(fShadowTextureSizeWidth));
	int iShadowSubHeight = std::min(static_cast<int>(std::lround(fVisibleHeightNow / fWorldTexelY)), static_cast<int>(fShadowTextureSizeHeight));
	int iShadowMinX = (static_cast<int>(fShadowTextureSizeWidth) - iShadowSubWidth) / 2;
	int iShadowMinY = (static_cast<int>(fShadowTextureSizeHeight) - iShadowSubHeight) / 2;
	rGlobalLayout.iShadowVisibleMinX = iShadowMinX;
	rGlobalLayout.iShadowVisibleMaxX = iShadowMinX + iShadowSubWidth;
	rGlobalLayout.iShadowVisibleMinY = iShadowMinY;
	rGlobalLayout.iShadowVisibleMaxY = iShadowMinY + iShadowSubHeight;

	// Sun-direction extension: extend by half the shadow-area width on the sun side. The elevation
	// texture is 1.5x wider than the shadow texture and rasterizes f4ShadowAreaExtra; the extension
	// fills exactly the extra half.
	rGlobalLayout.f4ShadowAreaExtra = rGlobalLayout.f4ShadowArea;
	float fHalfWidth = fFullWidth * 0.5f;
	if (fSunAngle >= XM_PI + XM_PIDIV2 || fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.f4ShadowAreaExtra.z += fHalfWidth;

		rGlobalLayout.fShadowWidthScale = fWorldTexelX;
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
		rGlobalLayout.f4ShadowAreaExtra.x -= fHalfWidth;

		rGlobalLayout.fShadowWidthScale = -fWorldTexelX;
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

static void PopulateTerrainParameters(shaders::GlobalLayout& rGlobalLayout, float fDayPercent, float fNoonPercent)
{
	// Terrain. fIslandHeight + fWaterDepth retired with the meters-everywhere refactor: heightmap
	// pixel values now carry absolute meters directly, so the shader no longer multiplies them.
	rGlobalLayout.fIslandAmbientOcclusion = fNoonPercent * gIslandAmbientOcclusion.Get();
	rGlobalLayout.fTerrainEarlyOut = gTerrainEarlyOut.Get();
	rGlobalLayout.fWaterEarlyOut = gWaterEarlyOut.Get();

	rGlobalLayout.fWaterReducedNormalOriginX = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginY = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginTwoX = 0.0f;
	rGlobalLayout.fWaterReducedNormalOriginTwoY = 0.0f;

	rGlobalLayout.fTerrainSnowBlend = gTerrainSnowBlend.Get();
	rGlobalLayout.fTerrainSnowAmbientOcclusionExclusion = gTerrainSnowAmbientOcclusionExclusion.Get();

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
	rGlobalLayout.fWaterDepthColorFloor = gWaterDepthColorFloor.Get();
	rGlobalLayout.fWaterUnderseaCompression = gWaterUnderseaCompression.Get();
	rGlobalLayout.fWaterDepthReflectionFeather = fDayPercent * gWaterDepthReflectionFeather.Get();
	rGlobalLayout.fWaterColorNoiseFrequency = gWaterColorNoiseFrequency.Get();

	rGlobalLayout.fWaterHighMultiplier = gWaterHighMultiplier.Get();
	rGlobalLayout.fWaterHighScaleOne = gWaterHighScaleOne.Get();
	rGlobalLayout.fWaterHighScaleTwo = gWaterHighScaleTwo.Get();

	if (fSunAngle >= XM_PIDIV16 && fSunAngle < XM_PIDIV2)
	{
		rGlobalLayout.fWaterDepthLutSunsetFade = 1.0f - (fSunAngle - XM_PIDIV16) / (XM_PIDIV2 - XM_PIDIV16);
	}
	else if (fSunAngle >= XM_PIDIV2 && fSunAngle < XM_PI - XM_PIDIV16)
	{
		rGlobalLayout.fWaterDepthLutSunsetFade = (fSunAngle - XM_PIDIV2) / (XM_PI - XM_PIDIV16 - XM_PIDIV2);
	}
	else
	{
		rGlobalLayout.fWaterDepthLutSunsetFade = 1.0f;
	}
	rGlobalLayout.fWaterDepthLutSunsetFade = gWaterDepthLutSunsetFadeIntensity.Get() * std::pow(rGlobalLayout.fWaterDepthLutSunsetFade, gWaterDepthLutSunsetFadePower.Get());

	rGlobalLayout.fWaterFresnel = std::pow(fDayPercent, 0.5f) * gWaterFresnel.Get();
	rGlobalLayout.fWaterColorBottom = gWaterColorBottom.Get();
	rGlobalLayout.fWaterColorHeightInv = 1.0f / gWaterColorHeight.Get();
	rGlobalLayout.fWaterColorNoiseAmount = gWaterColorNoiseAmount.Get();
	rGlobalLayout.fWaterColorNoiseWeightOne = gWaterColorNoiseWeightOne.Get();
	rGlobalLayout.fWaterColorNoiseWeightTwo = gWaterColorNoiseWeightTwo.Get();
	rGlobalLayout.fWaterColorNoiseMultiplierOne = gWaterColorNoiseMultiplierOne.Get();
	rGlobalLayout.fWaterColorNoiseMultiplierTwo = gWaterColorNoiseMultiplierTwo.Get();

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

	rGlobalLayout.fBeachFadeTop = gWaterBeachFadeTop.Get();
	rGlobalLayout.fBeachFadeInvRange = 1.0f / (gWaterBeachFadeBottom.Get() - gWaterBeachFadeTop.Get());
	rGlobalLayout.fWaterLowSteepness = gWaterLowSteepness.Get();

	rGlobalLayout.fWaterMediumSteepness = gWaterMediumSteepness.Get();
	rGlobalLayout.fWaterWaveNormalBlend = gWaterWaveNormalBlend.Get();
	rGlobalLayout.fWaterLowAmplitude = gWaterLowAmplitude.Get();
	rGlobalLayout.fWaterMediumAmplitude = gWaterMediumAmplitude.Get();
	rGlobalLayout.fWaterZOffsetTemp = gWaterZOffsetTemp.Get(); // DT: TEMP

	rGlobalLayout.iWaterLowCount = static_cast<int>(std::min(gWaterLowCount.Get<int64_t>(), static_cast<int64_t>(gWaterLowMax.Get())));
	rGlobalLayout.iWaterMediumCount = static_cast<int>(gWaterMediumCount.Get<int64_t>());

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
	// Camera-height-driven speed lerp — same factor as LightingUniforms.cpp.
	static constexpr float kfWaveFadeEnd = 2.0f * game::Camera::kfCameraEyeHeightDefault;
	float fCameraHeightZoomFactor = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, kfWaveFadeEnd, 0.0f, 1.0f);
	double dSpeed = static_cast<double>(std::lerp(gLightingSampledNormalsSpeedMin.Get(), gLightingSampledNormalsSpeedMax.Get(), fCameraHeightZoomFactor));
	// Per-sample reduced-time accumulators: integrate (size * speed * dt) per frame and fmod 10.0
	// rather than recomputing fmod(size * speed * t, 10.0). Per-frame integration keeps the UV
	// phase continuous when size or speed slide smoothly (e.g. zoom-driven speed lerp); the old
	// formulation produced a per-frame jump proportional to (deltaSpeed * t) that grew with playtime.
	static double sdReducedTimeOne = 0.0;
	static double sdReducedTimeTwo = 0.0;
	static double sdReducedTimeThree = 0.0;
	static float sfPrevElapsedTime = 0.0f;
	float fDeltaTime = std::max(0.0f, rGlobalLayout.fElapsedTime - sfPrevElapsedTime);
	sfPrevElapsedTime = rGlobalLayout.fElapsedTime;
	double dDeltaTime = static_cast<double>(fDeltaTime);
	sdReducedTimeOne   = std::fmod(sdReducedTimeOne   + dSizeBaseOne   * dSpeed * dDeltaTime, 10.0);
	sdReducedTimeTwo   = std::fmod(sdReducedTimeTwo   + dSizeBaseTwo   * dSpeed * dDeltaTime, 10.0);
	sdReducedTimeThree = std::fmod(sdReducedTimeThree + dSizeBaseThree * dSpeed * dDeltaTime, 10.0);
	double dCameraX = static_cast<double>(f4CameraPos.x);
	double dCameraY = static_cast<double>(f4CameraPos.y);

	// Per-sample reduced origin: rotate cameraXY on the CPU by the same R(-θ) the shader uses,
	// THEN fmod. This keeps the precision invariant under rotation: the reducedOrigin already
	// encodes the rotation, so the shader-side wrap shift is sizeMult*10 = integer (cleanly
	// absorbed by fract). If we instead let the shader rotate reducedOrigin, the wrap shift
	// becomes R*(sizeMult*10, 0) — non-integer for any θ that isn't a multiple of π/2 — and
	// produces a visible normal-pattern jump every time size*cameraXY crosses a multiple of 10.
	auto RotatedCamera = [&](float fRotation, double& rdOutX, double& rdOutY)
	{
		double dCos = static_cast<double>(std::cos(fRotation));
		double dSin = static_cast<double>(std::sin(fRotation));
		rdOutX = dCos * dCameraX + dSin * dCameraY;
		rdOutY = -dSin * dCameraX + dCos * dCameraY;
	};
	double dRotCameraXOne, dRotCameraYOne;
	double dRotCameraXTwo, dRotCameraYTwo;
	double dRotCameraXThree, dRotCameraYThree;
	RotatedCamera(gWaterNormalRotationOne.Get(),   dRotCameraXOne,   dRotCameraYOne);
	RotatedCamera(gWaterNormalRotationTwo.Get(),   dRotCameraXTwo,   dRotCameraYTwo);
	RotatedCamera(gWaterNormalRotationThree.Get(), dRotCameraXThree, dRotCameraYThree);

	rGlobalLayout.fWaterReducedNormalOriginX = static_cast<float>(std::fmod(dSizeBaseOne * dRotCameraXOne, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginY = static_cast<float>(std::fmod(dSizeBaseOne * dRotCameraYOne, 10.0));
	rGlobalLayout.fWaterReducedNormalTime = static_cast<float>(sdReducedTimeOne);
	rGlobalLayout.fWaterReducedNormalOriginTwoX = static_cast<float>(std::fmod(dSizeBaseTwo * dRotCameraXTwo, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginTwoY = static_cast<float>(std::fmod(dSizeBaseTwo * dRotCameraYTwo, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeTwo = static_cast<float>(sdReducedTimeTwo);
	rGlobalLayout.fWaterReducedNormalOriginThreeX = static_cast<float>(std::fmod(dSizeBaseThree * dRotCameraXThree, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginThreeY = static_cast<float>(std::fmod(dSizeBaseThree * dRotCameraYThree, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeThree = static_cast<float>(sdReducedTimeThree);

	// Modulus 10.0 (not 1.0) so the shader's per-sample multipliers fWaterColorNoiseMultiplierOne/Two
	// produce integer UV wraps for the calibrated defaults (0.2 * 10 = 2, 1.0 * 10 = 10) — fract()
	// then absorbs the wrap. Modulus 1.0 produced a sudden seam at the wrap radius because mult * 1.0
	// is non-integer for those defaults. Tuning gotcha: the sliders allow non-integer-tenths values
	// (e.g. 0.15) which break the integer-product property and the seam returns. Same constraint that
	// governs fWaterReducedNormalOrigin* above (where the per-octave multipliers are pinned).
	double dNoiseFreq = static_cast<double>(gWaterColorNoiseFrequency.Get());
	rGlobalLayout.fWaterReducedNoiseOriginX = static_cast<float>(std::fmod(dNoiseFreq * dCameraX, 10.0));
	rGlobalLayout.fWaterReducedNoiseOriginY = static_cast<float>(std::fmod(dNoiseFreq * dCameraY, 10.0));
}

void RenderFrameGlobal(int64_t iCommandBuffer, float fCurrentTime)
{
	RenderLightingGlobal(iCommandBuffer);
	RenderSmokeGlobal(iCommandBuffer);
	RenderWindGlobal(iCommandBuffer);

	float fSunAngle = game::gpCamera->SunAngle();

	// Global data
	shaders::GlobalLayout& rGlobalLayout = *reinterpret_cast<shaders::GlobalLayout*>(&gpBufferManager->mGlobalLayoutUniformBuffers.at(iCommandBuffer).mpMappedMemory[0]);

	rGlobalLayout.fElapsedTime = fCurrentTime;
	rGlobalLayout.fBaseHeight = gBaseHeight.Get();
	rGlobalLayout.fAspectRatio = gpSwapchainManager->mfAspectRatio;

	XMFLOAT4A f4CameraPosGlobal {};
	XMStoreFloat4A(&f4CameraPosGlobal, game::gpCamera->mVecPosition);
	rGlobalLayout.f2CameraPosition.x = f4CameraPosGlobal.x;
	rGlobalLayout.f2CameraPosition.y = f4CameraPosGlobal.y;
	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	// Lighting area: continuous visible-area world size (differs from the shadow-area path above, which
	// uses fixed-world-size texels in a pre-sized texture). f4RenderVisibleArea tracks
	// the camera frustum footprint and its width/height is continuous across mesh-LOD boundaries
	// (quad count halves but quad size doubles), so the lighting deposit/spread no longer pops 4x at
	// the LOD transitions (kfMinEyeHeight * 4^L = 600/2400/9600m). Origin is still snapped to integer
	// lighting-texel boundaries below, so XY pan stays shimmer-free (width is bit-stable within a
	// zoom bucket -> texel size constant -> snap moves in integer-texel steps). Zoom re-latches the
	// width once per integer-meter zoom bucket, a small accepted texel-scale step (vs. the old single
	// large pop per LOD).
	float fLightingWorldWidth = game::gpCamera->f4RenderVisibleArea.z - game::gpCamera->f4RenderVisibleArea.x;
	float fLightingWorldHeight = game::gpCamera->f4RenderVisibleArea.y - game::gpCamera->f4RenderVisibleArea.w;
	if (fLightingWorldWidth > 0.0f && fLightingWorldHeight > 0.0f)
	{
		auto [iLightingTextureX, iLightingTextureY] = TextureManager::DetailTextureSize(gLightingDepositTextureMultiplier.Get());
		float fTexelsX = static_cast<float>(iLightingTextureX);
		float fTexelsY = static_cast<float>(iLightingTextureY);

		float fTexelSizeX = fLightingWorldWidth / fTexelsX;
		float fTexelSizeY = fLightingWorldHeight / fTexelsY;

		XMFLOAT4A f4CameraPos {};
		XMStoreFloat4A(&f4CameraPos, game::gpCamera->mVecPosition);

		int64_t iLeftTexel = static_cast<int64_t>(std::floor((f4CameraPos.x - fLightingWorldWidth * 0.5f) / fTexelSizeX));
		int64_t iTopTexel = static_cast<int64_t>(std::floor((f4CameraPos.y + fLightingWorldHeight * 0.5f) / fTexelSizeY));
		float fLeft = static_cast<float>(iLeftTexel) * fTexelSizeX;
		float fTop = static_cast<float>(iTopTexel) * fTexelSizeY;

		rGlobalLayout.f4LightingArea = {fLeft, fTop, fLeft + fLightingWorldWidth, fTop - fLightingWorldHeight};

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
	PopulateTerrainParameters(rGlobalLayout, fDayPercent, fNoonPercent);
	PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent);

	// Debug
	rGlobalLayout.fDebugTextureIndex = gDebugTextureIndex.Get();
	rGlobalLayout.fDebugTextureFormat = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpDebugTextureFormats[static_cast<int64_t>(gDebugTextureIndex.Get())]);
	rGlobalLayout.fDebugTextureLinearRange = gMiscDebugTextureLinearRange.Get();

	rGlobalLayout.fSeaFloorElevation = gpIslandTerrain->mfSeaFloorElevation;
	// Zero-out line Terrain.vert uses to sink each island's submerged verts to the sea floor. Single source
	// of truth = the same constant DataPacker bakes the valid-area hull / texture masking from.
	rGlobalLayout.fUnderwaterMaskThreshold = common::kfUnderwaterMaskThresholdMeters * kfMetersToUnits;
	float fHigh = 0.0f;
	for (const auto& [rCrc, rIsland] : gpIslandTerrain->mIslands)
	{
		if (rIsland.mbGpuResident)
		{
			fHigh = std::max(fHigh, rIsland.mfWorldElevationMeters);
		}
	}
	rGlobalLayout.fDebugTerrainElevationHigh = fHigh;
}

} // namespace engine

#endif // defined(BT_CLIENT)
