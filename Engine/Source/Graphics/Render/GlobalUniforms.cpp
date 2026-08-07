#if defined(BT_CLIENT)

#include "Render.h"

#include "Graphics/Camera.h"
#include "Ui/HeightLerpWrapperQuartet.h"
#include "Ui/LightingWrappersBase.h"
#include "Ui/MiscWrappersBase.h"
#include "Ui/PbrWrappersBase.h"
#include "Ui/ShadowWrappersBase.h"
#include "Ui/SmokeWrappersBase.h"
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

// Sun/Moon direction + tilt: stores the normalized sky direction in f4SunMoonNormal and returns it so the
// shadow-stretch translation (which also needs it) can be folded CPU-side without re-reading write-only memory.
static XMVECTOR XM_CALLCONV PopulateSunMoonDirection(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
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

	// Uniform-only sun-normal products folded here (both need the finished sun/moon normal): Terrain.frag's
	// snow sun-normal tilt (fTerrainSnowBlend * normal) and Water.frag's skybox biased sun normal.
	XMStoreFloat4(&rGlobalLayout.f4TerrainSnowSunNormal, XMVectorScale(vecSunMoonNormal, gTerrainSnowBlend.Get()));
	XMVECTOR vecBiasedSunNormal = XMVector3Normalize(XMVectorAdd(vecSunMoonNormal, XMVectorSet(0.0f, 0.0f, gLightingWaterSkyboxSunBias.Get(), 0.0f)));
	XMStoreFloat4(&rGlobalLayout.f4WaterBiasedSunNormal, vecBiasedSunNormal);

	return vecSunMoonNormal;
}

// Piecewise day-cycle sun/moon color + ambient ramp, the sun/moon intensity stores, and the precomputed
// uniform-only sun/moon/ambient products consumed by SunLighting, the smoke helpers, and Water.frag. Returns
// the finished ambient color so the shadow-ambient split can be folded without re-reading write-only memory.
static XMVECTOR XM_CALLCONV PopulateDayCycleColors(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
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

	// Per-target sun/moon intensities. Terrain/Water/Objects are captured as locals and pre-folded into the
	// precomputed products below (their layout fields were retired); Smoke stays as a raw multiplier.
	float fSunIntensityTerrain = gSunMoonSunIntensityTerrain.Get();
	float fMoonIntensityTerrain = gSunMoonMoonIntensityTerrain.Get();
	float fSunIntensityWater = gSunMoonSunIntensityWater.Get();
	float fMoonIntensityWater = gSunMoonMoonIntensityWater.Get();
	float fSunIntensitySmoke = gSunMoonSunIntensitySmoke.Get();
	float fMoonIntensitySmoke = gSunMoonMoonIntensitySmoke.Get();
	float fSunIntensityObjects = gSunMoonSunIntensityObjects.Get();
	float fMoonIntensityObjects = gSunMoonMoonIntensityObjects.Get();
	rGlobalLayout.fSunIntensitySmoke    = fSunIntensitySmoke;
	rGlobalLayout.fMoonIntensitySmoke   = fMoonIntensitySmoke;

	vecAmbient = XMVectorSetW(XMVectorScale(vecAmbient, gSunMoonAmbientMultiplier.Get()), 1.0f);
	XMStoreFloat4(&rGlobalLayout.f4AmbientColor, vecAmbient);

	// Precomputed day-cycle products (every operand is invocation-invariant): SunLighting (terrain), the smoke
	// helpers (AddSmoke/BlendSmokePrecomputed), and Water.frag. Folding here removes the equivalent per-pixel work.
	const XMVECTOR vecRec601 = XMVectorSet(0.299f, 0.587f, 0.114f, 0.0f);

	// Terrain sun/moon (SunLighting): per-target-scaled colors + Rec.601 magnitudes for the ambient-shadow blend.
	XMVECTOR vecSunTerrain = XMVectorScale(vecSunMoon, fSunIntensityTerrain);
	XMVECTOR vecMoonTerrain = XMVectorScale(vecMoon, fMoonIntensityTerrain);
	XMStoreFloat4(&rGlobalLayout.f4SunColorTerrain, vecSunTerrain);
	XMStoreFloat4(&rGlobalLayout.f4MoonColorTerrain, vecMoonTerrain);
	float fSunMagTerrain = XMVectorGetX(XMVector3Dot(vecSunTerrain, vecRec601));
	float fMoonMagTerrain = XMVectorGetX(XMVector3Dot(vecMoonTerrain, vecRec601));
	rGlobalLayout.fSunMagTerrain = fSunMagTerrain;
	rGlobalLayout.fMoonMagTerrain = fMoonMagTerrain;
	rGlobalLayout.fSunMoonMagSumInvTerrain = 1.0f / std::max(fSunMagTerrain + fMoonMagTerrain, 0.001f);

	// Smoke base lighting (AddSmoke/BlendSmokePrecomputed): componentwise max(sun, moon) sky term + ambient.
	XMVECTOR vecSmokeBase = XMVectorAdd(XMVectorMax(XMVectorScale(vecSunMoon, fSunIntensitySmoke), XMVectorScale(vecMoon, fMoonIntensitySmoke)), vecAmbient);
	XMStoreFloat4(&rGlobalLayout.f4SmokeBaseLighting, vecSmokeBase);

	// Water sun/moon (Water.frag): max-combine for skybox tint + its /3 scalar, the /3 ambient scalar, and the
	// Rec.601 effective-shadow weights with their guarded reciprocal-sum.
	XMVECTOR vecWaterSun = XMVectorScale(vecSunMoon, fSunIntensityWater);
	XMVECTOR vecWaterMoon = XMVectorScale(vecMoon, fMoonIntensityWater);
	XMVECTOR vecWaterSunOrMoon = XMVectorMax(vecWaterSun, vecWaterMoon);
	XMStoreFloat4(&rGlobalLayout.f4WaterSunOrMoon, vecWaterSunOrMoon);
	rGlobalLayout.fWaterSunScalar = (XMVectorGetX(vecWaterSunOrMoon) + XMVectorGetY(vecWaterSunOrMoon) + XMVectorGetZ(vecWaterSunOrMoon)) / 3.0f;
	rGlobalLayout.fWaterAmbientScalar = (XMVectorGetX(vecAmbient) + XMVectorGetY(vecAmbient) + XMVectorGetZ(vecAmbient)) / 3.0f;
	float fWaterSunWeight = XMVectorGetX(XMVector3Dot(vecWaterSun, vecRec601));
	float fWaterMoonWeight = XMVectorGetX(XMVector3Dot(vecWaterMoon, vecRec601));
	rGlobalLayout.fWaterSunWeight = fWaterSunWeight;
	rGlobalLayout.fWaterMoonWeight = fWaterMoonWeight;
	rGlobalLayout.fWaterShadowWeightSumInv = 1.0f / std::max(0.001f, fWaterSunWeight + fWaterMoonWeight);

	// Objects (Model.frag) sun/moon products. fPbrSun / fPbrDayBrightness are Main-phase PBR tunables, folded
	// here because the day-cycle sun/moon colors are only CPU-local in this global phase (reading them back from
	// the mapped f4SunColor/f4MoonColor would stall on write-combined memory). The direct BRDF term folds the
	// per-target Objects intensity and fPbrSun; the IBL specular term additionally folds the Rec.709 luminance of
	// the UNSCALED color over fPbrDayBrightness — kept as a separate field so the IBL path stays linear in fPbrSun
	// (folding fPbrSun into the luminance would compound to fPbrSun^3). Unguarded reciprocal reproduces Model.frag's
	// original divide by fPbrDayBrightness.
	const XMVECTOR vecRec709 = XMVectorSet(0.2126f, 0.7152f, 0.0722f, 0.0f);
	float fPbrSun = gPbrSun.Get();
	float fPbrDayBrightnessInv = 1.0f / gPbrDayBrightness.Get();
	XMVECTOR vecPbrSunColorObjects = XMVectorScale(vecSunMoon, fSunIntensityObjects * fPbrSun);
	XMVECTOR vecPbrMoonColorObjects = XMVectorScale(vecMoon, fMoonIntensityObjects * fPbrSun);
	XMStoreFloat4(&rGlobalLayout.f4PbrSunColorObjects, vecPbrSunColorObjects);
	XMStoreFloat4(&rGlobalLayout.f4PbrMoonColorObjects, vecPbrMoonColorObjects);
	XMStoreFloat4(&rGlobalLayout.f4PbrSunColorObjectsIbl, XMVectorScale(vecPbrSunColorObjects, XMVectorGetX(XMVector3Dot(vecSunMoon, vecRec709)) * fPbrDayBrightnessInv));
	XMStoreFloat4(&rGlobalLayout.f4PbrMoonColorObjectsIbl, XMVectorScale(vecPbrMoonColorObjects, XMVectorGetX(XMVector3Dot(vecMoon, vecRec709)) * fPbrDayBrightnessInv));

	return vecAmbient;
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

static void PopulateSunAndLighting(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float& rfDayPercent, float& rfNoonPercent, XMVECTOR& rvecSunMoonNormalOut, XMVECTOR& rvecAmbientColorOut)
{
	rvecSunMoonNormalOut = PopulateSunMoonDirection(rGlobalLayout, fSunAngle);
	rvecAmbientColorOut = PopulateDayCycleColors(rGlobalLayout, fSunAngle);
	PopulateDayCycleFeatherWindows(fSunAngle, rfDayPercent, rfNoonPercent);

	// Time of day — resolved alongside the day-cycle derivation, where every other day-cycle product is computed (Lighting region owns these fields).
	float fLightingTimeOfDayMultiplier = rfDayPercent * gLightingDayFinalMultiplier.Get() + (1.0f - rfDayPercent) * gLightingNightFinalMultiplier.Get();
	rGlobalLayout.fLightingTimeOfDayMultiplier = fLightingTimeOfDayMultiplier;
	// Smoke lighting multiplier folded with time-of-day (AddSmoke/BlendSmokePrecomputed): both inputs are known
	// here (fSmokeLightingMultiplier source from RenderSmokeGlobal earlier this frame, time-of-day just above).
	rGlobalLayout.fSmokeLightingCombinedMultiplier = fLightingTimeOfDayMultiplier * gSmokeLightingMultiplier.Get();
	rGlobalLayout.fLightingWaterSkyboxOne = gLightingWaterSkyboxOne.Get() + (1.0f - rfDayPercent) * 1.5f * gLightingWaterSkyboxOne.Get();
	// Skybox normal soften blends a sunrise/sunset (low-sun) value toward a noon value by the noon feather (1 at solar noon, 0 toward both horizons / night).
	rGlobalLayout.fLightingWaterSkyboxNormalSoften = (1.0f - rfNoonPercent) * gLightingWaterSkyboxNormalSoftenSunrise.Get() + rfNoonPercent * gLightingWaterSkyboxNormalSoftenNoon.Get();
}

static void XM_CALLCONV PopulateShadowStretch(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, FXMVECTOR vecSunMoonNormal)
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

	// Pre-cube the scaled stretch offsets (ShadowStretchProjection cubes them per-pixel) and fold the base
	// translation (sum of cubes) * -f4SunMoonNormal.xy — both uniform-only. fStretchX (position-diff dependent) stays in-shader.
	float fStretchScale = gObjectShadowsSunsetStretch.Get();
	float fSunriseStretchScaled = fSunriseStretch * fStretchScale;
	float fSunsetStretchScaled = fSunsetStretch * fStretchScale;
	float fSunriseStretchCubed = fSunriseStretchScaled * fSunriseStretchScaled * fSunriseStretchScaled;
	float fSunsetStretchCubed = fSunsetStretchScaled * fSunsetStretchScaled * fSunsetStretchScaled;
	rGlobalLayout.fShadowSunriseStretchCubed = fSunriseStretchCubed;
	rGlobalLayout.fShadowSunsetStretchCubed = fSunsetStretchCubed;
	float fSumCubes = fSunriseStretchCubed + fSunsetStretchCubed;
	rGlobalLayout.f2ShadowStretchTranslation.x = -fSumCubes * XMVectorGetX(vecSunMoonNormal);
	rGlobalLayout.f2ShadowStretchTranslation.y = -fSumCubes * XMVectorGetY(vecSunMoonNormal);
}

static void PopulateShadowArea(shaders::GlobalLayout& rGlobalLayout, float fShadowTextureSizeWidth, float fShadowTextureSizeHeight, float& rfWorldTexelX, float& rfFullWidth)
{
	// Shadow area: world-sized texels from a coverage-safe camera-height reference. The reference expands immediately
	// with outward zoom and contracts at its existing rate on inward zoom, so it is never below the live eye height.
	// The texel world size is sized so a constant on-screen pixel count
	// (textureWidth / kfShadowHeadroomMultiplier) spans the live straight-down frustum width at the camera's
	// mfShadowTexelEyeHeight, so it is fixed at a settled height (the grid snaps cleanly under XY pan -> no shimmer)
	// and only rescales immediately outward or gradually inward. The headroom keeps the live render area inside the
	// full footprint throughout either transition. Reading the actual (clamped)
	// extent keeps the world coverage device-clamp-invariant.
	// f4ShadowArea is the full footprint, camera-centered and snapped to the current texel grid (integer-texel pan).
	// The base texel derives from the analytic straight-down frustum width (gFov/aspect), never the snapped
	// render-area width, so it stays bit-stable at a settled height.
	WorldSizedTexelArea area = ComputeWorldSizedTexelArea(game::Camera::kfShadowHeadroomMultiplier, game::gpCamera->mfShadowTexelEyeHeight, fShadowTextureSizeWidth, fShadowTextureSizeHeight, gpSwapchainManager->mfAspectRatio, gFov.Get(), game::gpCamera->mVecPosition);
	rGlobalLayout.f4ShadowArea = area.f4Area;

	// Latch the previous world area. A recreate makes the previous area current and forces pure-current temporal
	// output; otherwise ShadowTemporal.comp rejects reprojected samples that land outside the previous footprint.
	static TemporalAreaLatch sTemporalAreaLatch {};
	rGlobalLayout.fShadowTemporalBlend = sTemporalAreaLatch.Update(area.f4Area, gbShadowTemporalReset, gShadowTemporalBlend.Get(), rGlobalLayout.f4ShadowAreaPrevious);

	rfWorldTexelX = area.fWorldTexelX;
	rfFullWidth = area.fFullWidth;
}

static void PopulateShadowSunExtension(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fWorldTexelX, float fFullWidth, float fShadowElevationTextureSizeWidth, float fShadowTextureSizeWidth, float& rfShadowSunAngle)
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
			rfShadowSunAngle = fSunAngle;
		}
		else
		{
			rfShadowSunAngle = XM_2PI - fSunAngle;
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
		rfShadowSunAngle = fSunAngle >= XM_PI ? fSunAngle - XM_PI : XM_PI - fSunAngle;
		rGlobalLayout.fShadowDirectionMultiplier = -1.0f;

		rGlobalLayout.iShadowElevationSize = 0;
		rGlobalLayout.iShadowIncrement = -1;
		rGlobalLayout.iShadowStartOffset = static_cast<int32_t>(fShadowElevationTextureSizeWidth - fShadowTextureSizeWidth); // Elevation extension half-width, from real extents.
	}
}

static void XM_CALLCONV PopulateShadowParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent, float fNoonPercent, FXMVECTOR vecSunMoonNormal, FXMVECTOR vecAmbientColor)
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
	// Kept as a local (field removed): folded into fShadowAngleOffsetSum after the sun extension resolves the shadow sun angle.
	float fShadowNoonOffset = fOffsetNoon * gShadowFeatherNoonOffset.Get();

	float fShadowDistanceFalloff = gShadowDistanceFalloff.Get();
	rGlobalLayout.fShadowDistanceFalloff = fShadowDistanceFalloff;
	rGlobalLayout.fShadowDistanceFalloffInv = 1.0f / fShadowDistanceFalloff;

	// Separable Gaussian blur kernel precomputed for ShadowBlurH/V: the kernel is symmetric, so store the
	// half-kernel [0, radius] and the reciprocal of the full-span weight sum (center counts once, each side twice).
	float fShadowBlurSigma = std::max(gShadowBlurSigma.Get(), 1e-6f);
	float fShadowBlurWeightSum = 0.0f;
	for (int32_t i = 0; i <= shaders::kiShadowBlurRadius; ++i)
	{
		float fA = static_cast<float>(i) / fShadowBlurSigma;
		float fWeight = std::exp(-0.5f * fA * fA);
		rGlobalLayout.pfShadowBlurWeights[i] = fWeight;
		fShadowBlurWeightSum += (i == 0) ? fWeight : 2.0f * fWeight;
	}
	rGlobalLayout.fShadowBlurWeightSumInv = 1.0f / fShadowBlurWeightSum;

	rGlobalLayout.fObjectShadowsBlurSigma = gObjectShadowsBlurSigma.Get();
	rGlobalLayout.iObjectShadowsBlurRadius = static_cast<int32_t>(gObjectShadowsBlurRadius.Get());
	rGlobalLayout.fObjectShadowsGrow = gObjectShadowsGrow.Get();
	rGlobalLayout.fObjectShadowsIntensity = fDayPercent * gObjectShadowsNoon.Get() + (1.0f - fDayPercent) * gObjectShadowsSunset.Get();
	rGlobalLayout.fObjectShadowsIntensity *= std::pow(fDayPercent, 0.1f);
	// Kept as a local (field removed): folded into fShadowAngleOffsetSum below.
	float fShadowSunsetOffset = fShadowEvening * gShadowFeatherSunsetOffset.Get();

	PopulateShadowStretch(rGlobalLayout, fSunAngle, vecSunMoonNormal);
	// fShadowAffectAmbient and the two f4AmbientColor products (SunLighting ambient split) are uniform-only;
	// fold the split here where both fShadowAffectAmbient and the finished ambient color are CPU-local.
	float fShadowAffectAmbient = std::pow(fDayPercent, 0.25f) * gShadowAffectAmbient.Get();
	rGlobalLayout.fShadowAffectAmbient = fShadowAffectAmbient;
	XMStoreFloat4(&rGlobalLayout.f4AmbientUnshadowed, XMVectorScale(vecAmbientColor, 1.0f - fShadowAffectAmbient));
	XMStoreFloat4(&rGlobalLayout.f4AmbientShadowed, XMVectorScale(vecAmbientColor, fShadowAffectAmbient));

	rGlobalLayout.f2ShadowTextureSizeInv.x = 1.0f / fShadowTextureSizeWidth;
	rGlobalLayout.f2ShadowTextureSizeInv.y = 1.0f / fShadowTextureSizeHeight;
	rGlobalLayout.f2ShadowElevationTextureSizeInv.x = 1.0f / fShadowElevationTextureSizeWidth;
	rGlobalLayout.f2ShadowElevationTextureSizeInv.y = 1.0f / fShadowTextureSizeHeight; // Elevation texture shares the shadow texture's height.

	float fShadowHeightFadeTop = gShadowHeightFadeTop.Get();
	float fShadowHeightFadeBottom = gShadowHeightFadeBottom.Get();
	rGlobalLayout.fShadowHeightFadeBottom = fShadowHeightFadeBottom;
	// No divide-by-zero guard: reproduces Shadow.comp's original unguarded divide exactly. Fade top is in
	// [0,20] and bottom in [-30,0], so the range is >= 0; the only degenerate case top==bottom==0 yields +inf,
	// which the shader's final clamp already absorbed as the prior 1/0 did.
	rGlobalLayout.fShadowHeightFadeRangeInv = 1.0f / (fShadowHeightFadeTop - fShadowHeightFadeBottom);

	float fWorldTexelX = 0.0f;
	float fFullWidth = 0.0f;
	PopulateShadowArea(rGlobalLayout, fShadowTextureSizeWidth, fShadowTextureSizeHeight, fWorldTexelX, fFullWidth);

	float fShadowSunAngle = 0.0f;
	PopulateShadowSunExtension(rGlobalLayout, fSunAngle, fWorldTexelX, fFullWidth, fShadowElevationTextureSizeWidth, fShadowTextureSizeWidth, fShadowSunAngle);

	// Angle sub-sum hoisted from Shadow.comp's per-occluder feather term (shadow sun angle + noon + sunset offsets).
	rGlobalLayout.fShadowAngleOffsetSum = fShadowSunAngle + fShadowNoonOffset + fShadowSunsetOffset;

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

	// fTerrainSnowBlend folds into f4TerrainSnowSunNormal (PopulateSunMoonDirection); no standalone field remains.
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
	float fBaseHeight = gBaseHeight.Get();
	rGlobalLayout.fBaseHeight = fBaseHeight;
	rGlobalLayout.fBaseHeightInv = 1.0f / std::max(fBaseHeight, 0.001f);
	rGlobalLayout.fAspectRatioInv = 1.0f / gpSwapchainManager->mfAspectRatio;

	XMFLOAT4A f4CameraPosGlobal {};
	XMStoreFloat4A(&f4CameraPosGlobal, game::gpCamera->mVecPosition);
	rGlobalLayout.f2CameraPosition.x = f4CameraPosGlobal.x;
	rGlobalLayout.f2CameraPosition.y = f4CameraPosGlobal.y;
	rGlobalLayout.f4VisibleArea = game::gpCamera->f4RenderVisibleArea;

	float fDayPercent = 0.0f;
	float fNoonPercent = 0.0f;
	XMVECTOR vecSunMoonNormal = XMVectorZero();
	XMVECTOR vecAmbientColor = XMVectorZero();
	PopulateSunAndLighting(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent, vecSunMoonNormal, vecAmbientColor);
	PopulateShadowParameters(rGlobalLayout, fSunAngle, fDayPercent, fNoonPercent, vecSunMoonNormal, vecAmbientColor);
	PopulateTerrainParameters(rGlobalLayout, fDayPercent, fNoonPercent);
	PopulateWaterParameters(rGlobalLayout, fSunAngle, fDayPercent);

	// Debug
	rGlobalLayout.fDebugTextureIndex = gDebugTextureIndex.Get();
	rGlobalLayout.fDebugTextureFormat = static_cast<float>(gpTextureManager->mRenderTargetTextures.mpDebugTextureFormats[static_cast<int64_t>(gDebugTextureIndex.Get())]);
	rGlobalLayout.fDebugTextureLinearRange = gMiscDebugTextureLinearRange.Get();

	float fSeaFloorElevation = gpIslandTerrain->mfSeaFloorElevation;
	rGlobalLayout.fSeaFloorElevation = fSeaFloorElevation;
	rGlobalLayout.fSeaFloorElevationInv = 1.0f / fSeaFloorElevation;
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
