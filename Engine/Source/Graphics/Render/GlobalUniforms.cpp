#if defined(BT_CLIENT)

#include "Render.h"

#include "Graphics/Camera.h"
#include "Ui/HeightLerpWrapperQuartet.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/SunMoonWrappersBase.h"
#include "Ui/TerrainWrappersBase.h"
#include "Ui/WaterWrappersBase.h"
#include "Ui/WrapperBase.h"

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

// Sun/Moon direction + tilt: stores the normalized sky direction in f4SunMoonNormal.
static void PopulateSunMoonDirection(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
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
}

// Piecewise day-cycle sun/moon color + ambient ramp, plus the sun/moon intensity stores.
static void PopulateDayCycleColors(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
{
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
}

// Noon/day feather windows: resolve the day-cycle blend percents from the sun angle.
static void PopulateDayCycleFeatherWindows(float fSunAngle, float& rfDayPercent, float& rfNoonPercent)
{
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

static void PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float& rfDayPercent, float& rfNoonPercent)
{
	PopulateSunMoonDirection(rGlobalLayout, fSunAngle);
	PopulateDayCycleColors(rGlobalLayout, fSunAngle);
	PopulateDayCycleFeatherWindows(fSunAngle, rfDayPercent, rfNoonPercent);

	// Time of day — resolved alongside the day-cycle derivation, where every other day-cycle product is computed (Lighting region owns these fields).
	rGlobalLayout.fLightingTimeOfDayMultiplier = rfDayPercent * gLightingDayFinalMultiplier.Get() + (1.0f - rfDayPercent) * gLightingNightFinalMultiplier.Get();
	rGlobalLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - rfDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();
	// Skybox normal soften blends a sunrise/sunset (low-sun) value toward a noon value by the noon feather (1 at solar noon, 0 toward both horizons / night).
	rGlobalLayout.fLightingWaterSkyboxNormalSoften = (1.0f - rfNoonPercent) * gLightingWaterSkyboxNormalSoftenSunrise.Get() + rfNoonPercent * gLightingWaterSkyboxNormalSoftenNoon.Get();
}

static void PopulateShadowStretch(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
{
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
		// Midday: no horizon stretch — this explicit window documents the angle coverage between the sunrise and sunset stretch ramps.
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
}

static void PopulateShadowArea(shaders::GlobalLayout& rGlobalLayout, float fShadowTextureSizeWidth, float fShadowTextureSizeHeight, float& rfWorldTexelX, float& rfFullWidth)
{
	// Shadow area: ramped-world-size texels. The texel world size is sized so a constant on-screen pixel count
	// (textureWidth / kfShadowHeadroomMultiplier) spans the live straight-down frustum width at the camera's
	// rate-limited mfShadowTexelEyeHeight -- so it is fixed at a settled height (the grid snaps cleanly under XY pan
	// -> no shimmer) and only rescales while the ramp tracks a zoom. The steady-state ray-marched sub-window is
	// therefore textureWidth / kfShadowHeadroomMultiplier at every settled height; a fast zoom-out transiently grows
	// it toward the full texture (then the CLAMP_TO_BORDER edge covers any overflow). Reading the actual (clamped)
	// extent keeps the world coverage device-clamp-invariant; the headroom multiplier cancels out of the window count.
	// f4ShadowArea is the full footprint, camera-centered and snapped to the current texel grid (integer-texel pan).
	// The base texel derives from the analytic straight-down frustum width (gFov/aspect), never the snapped
	// render-area width, so it stays bit-stable at a settled height.
	WorldSizedTexelArea area = ComputeWorldSizedTexelArea(game::Camera::kfShadowHeadroomMultiplier, game::gpCamera->mfShadowTexelEyeHeight, fShadowTextureSizeWidth, fShadowTextureSizeHeight, gpSwapchainManager->mfAspectRatio, gFov.Get(), game::gpCamera->mVecPosition);
	rGlobalLayout.f4ShadowArea = area.f4Area;

	// Temporal accumulation: feed the previous frame's shadow area so ShadowTemporal.comp can reproject the
	// history into the current grid (mirrors the smoke/wind previous-area latch). First frame: previous ==
	// current and blend forced to 1.0 (pure current) so the uninitialized history texture is never shown; that
	// frame's copy seeds valid history. Once-per-frame latch (RenderFrameGlobal runs once per frame).
	// A Graphics recreate (device-lost / settings) rebuilt mShadowHistoryTexture with undefined contents while
	// this static survived. The reset re-arms the first-frame guard so this frame blends pure-current and re-seeds history.
	static TemporalAreaLatch sTemporalAreaLatch {};
	rGlobalLayout.fShadowTemporalBlend = sTemporalAreaLatch.Update(rGlobalLayout.f4ShadowArea, gbShadowTemporalReset, gShadowTemporalBlend.Get(), rGlobalLayout.f4ShadowAreaPrevious);

	// Centered visible window (texels) from the live eye height — only this region is ray-marched. With the texel grid
	// tracking live height at all heights, the window is constant on-screen at a settled height; the min() against the
	// texture extent only caps the transient when a fast zoom-out outruns the ramp (overflow reads no-shadow via the border).
	XMFLOAT2 f2VisibleAreaNow = area.ComputeVisibleArea(game::gpCamera->mfCameraEyeHeight);
	int64_t iShadowSubWidth = std::min(static_cast<int64_t>(std::llround(f2VisibleAreaNow.x / area.fWorldTexelX)), static_cast<int64_t>(fShadowTextureSizeWidth));
	int64_t iShadowSubHeight = std::min(static_cast<int64_t>(std::llround(f2VisibleAreaNow.y / area.fWorldTexelY)), static_cast<int64_t>(fShadowTextureSizeHeight));
	int64_t iShadowMinX = (static_cast<int64_t>(fShadowTextureSizeWidth) - iShadowSubWidth) / 2;
	int64_t iShadowMinY = (static_cast<int64_t>(fShadowTextureSizeHeight) - iShadowSubHeight) / 2;
	rGlobalLayout.iShadowVisibleMinX = static_cast<int32_t>(iShadowMinX);
	rGlobalLayout.iShadowVisibleMaxX = static_cast<int32_t>(iShadowMinX + iShadowSubWidth);
	rGlobalLayout.iShadowVisibleMinY = static_cast<int32_t>(iShadowMinY);
	rGlobalLayout.iShadowVisibleMaxY = static_cast<int32_t>(iShadowMinY + iShadowSubHeight);
	giShadowActivePixelsX = iShadowSubWidth;
	giShadowActivePixelsY = iShadowSubHeight;

	rfWorldTexelX = area.fWorldTexelX;
	rfFullWidth = area.fFullWidth;
}

static void PopulateShadowSunExtension(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fWorldTexelX, float fFullWidth, float fShadowElevationTextureSizeWidth, float fShadowTextureSizeWidth)
{
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

		rGlobalLayout.iShadowElevationSize = static_cast<int32_t>(fShadowElevationTextureSizeWidth);
		rGlobalLayout.iShadowIncrement = 1;
		rGlobalLayout.iShadowStartOffset = 0;
	}
	else
	{
		rGlobalLayout.f4ShadowAreaExtra.x -= fHalfWidth;

		rGlobalLayout.fShadowWidthScale = -fWorldTexelX;
		rGlobalLayout.fShadowSunAngle = fSunAngle >= XM_PI ? fSunAngle - XM_PI : XM_PI - fSunAngle;
		rGlobalLayout.fShadowDirectionMultiplier = -1.0f;

		rGlobalLayout.iShadowElevationSize = 0;
		rGlobalLayout.iShadowIncrement = -1;
		rGlobalLayout.iShadowStartOffset = static_cast<int32_t>(fShadowElevationTextureSizeWidth - fShadowTextureSizeWidth); // Elevation extension half-width, from real extents.
	}
}

static void PopulateShadowParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent, float fNoonPercent)
{
	// Shadow texture
	VkExtent3D vkShadowTextureExtent = gpTextureManager->mRenderTargetTextures.mShadowTexture.mInfo.extent;
	float fShadowTextureSizeWidth = static_cast<float>(vkShadowTextureExtent.width);
	float fShadowTextureSizeHeight = static_cast<float>(vkShadowTextureExtent.height);
	// 1.5x-wide elevation texture: read the created extent so the headroom factor has a single owner at the
	// allocation site (RenderTargetTextures::CreateShadowTextures), like the shadow extent read just above.
	float fShadowElevationTextureSizeWidth = static_cast<float>(gpTextureManager->mRenderTargetTextures.mShadowElevationTexture.mInfo.extent.width);

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

	PopulateShadowStretch(rGlobalLayout, fSunAngle);
	rGlobalLayout.fShadowAffectAmbient = std::pow(fDayPercent, 0.25f) * gShadowAffectAmbient.Get();

	rGlobalLayout.fShadowTextureSizeWidth = fShadowTextureSizeWidth;
	rGlobalLayout.fShadowTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowElevationTextureSizeWidth = fShadowElevationTextureSizeWidth;
	rGlobalLayout.fShadowElevationTextureSizeHeight = fShadowTextureSizeHeight;
	rGlobalLayout.fShadowHeightFadeTop = gShadowHeightFadeTop.Get();
	rGlobalLayout.fShadowHeightFadeBottom = gShadowHeightFadeBottom.Get();

	rGlobalLayout.iShadowTextureWidth = static_cast<int32_t>(vkShadowTextureExtent.width);
	rGlobalLayout.iShadowTextureHeight = static_cast<int32_t>(vkShadowTextureExtent.height);

	float fWorldTexelX = 0.0f;
	float fFullWidth = 0.0f;
	PopulateShadowArea(rGlobalLayout, fShadowTextureSizeWidth, fShadowTextureSizeHeight, fWorldTexelX, fFullWidth);

	PopulateShadowSunExtension(rGlobalLayout, fSunAngle, fWorldTexelX, fFullWidth, fShadowElevationTextureSizeWidth, fShadowTextureSizeWidth);

	// Shadow multiplier: 1.0 during day, gSunMoonShadowNightMultiplier at night.
	// Derived from the shared night-amount envelope so the sun/moon split and the shadow
	// night-gate stay in lockstep.
	rGlobalLayout.fShadowMoonMultiplier = std::lerp(1.0f, gSunMoonShadowNightMultiplier.Get(), ComputeNightAmount(fSunAngle));
}

static void PopulateTerrainParameters(shaders::GlobalLayout& rGlobalLayout, float fDayPercent, float fNoonPercent)
{
	// Terrain. Heightmap pixel values carry absolute meters directly, so the shader applies no
	// height/depth multiplier.
	rGlobalLayout.fIslandAmbientOcclusion = fNoonPercent * gIslandAmbientOcclusion.Get();

	rGlobalLayout.fTerrainSnowBlend = gTerrainSnowBlend.Get();
	rGlobalLayout.fTerrainSnowAmbientOcclusionExclusion = gTerrainSnowAmbientOcclusionExclusion.Get();
	const float fTerrainDetailNormalsMultiplier = gTerrainDetailNormalsMultiplier.Resolve(game::gpCamera->mfCameraEyeHeight);

	rGlobalLayout.fTerrainRockSize = gTerrainRockSize.Get();
	rGlobalLayout.fTerrainRockBlend = gTerrainRockBlend.Get();
	rGlobalLayout.fTerrainRockNormalsSizeOne = gTerrainRockNormalsSizeOne.Get();
	rGlobalLayout.fTerrainRockNormalsSizeTwo = gTerrainRockNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainRockNormalsSizeThree = gTerrainRockNormalsSizeThree.Get();
	rGlobalLayout.fTerrainRockNormalsBlend = fTerrainDetailNormalsMultiplier * gTerrainRockNormalsBlend.Get();

	rGlobalLayout.fTerrainBeachSandSize = gTerrainBeachSandSize.Get();
	rGlobalLayout.fTerrainBeachSandBlend = gTerrainBeachSandBlend.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeOne = gTerrainBeachNormalsSizeOne.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeTwo = gTerrainBeachNormalsSizeTwo.Get();
	rGlobalLayout.fTerrainBeachNormalsSizeThree = gTerrainBeachNormalsSizeThree.Get();
	rGlobalLayout.fTerrainBeachNormalsBlend = std::max(fDayPercent * fDayPercent, 0.25f) * gTerrainBeachNormalsBlend.Get() * fTerrainDetailNormalsMultiplier;
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
	rGlobalLayout.fUnderwaterMaskThreshold = common::kfUnderwaterMaskThresholdMeters;
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
