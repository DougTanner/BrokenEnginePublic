#if defined(BT_CLIENT)

#include "Render.h"

#include "Graphics/Camera.h"
#include "Ui/HeightLerpWrapperQuartet.h"
#include "Ui/WaterWrappersBase.h"

namespace engine
{

// Water depth-LUT sunset fade: piecewise over the sun angle, then intensity/power shaping.
static void PopulateWaterSunsetFade(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
{
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
}

// Water directional term: piecewise over the sun angle, squared.
static void PopulateWaterDirectional(shaders::GlobalLayout& rGlobalLayout, float fSunAngle)
{
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
}

// Camera-relative UV reduction — owns the per-frame reduced-time accumulator latches, so it must be
// called exactly once per frame (preserved by the single PopulateWaterParameters call site).
static void PopulateWaterReducedUv(shaders::GlobalLayout& rGlobalLayout)
{
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
	// Camera-height-driven speed lerp — single-sourced fade endpoint shared with LightingUniforms.cpp.
	float fCameraHeightZoomFactor = engine::LerpAtHeight(game::gpCamera->mfCameraEyeHeight, game::Camera::kfCameraEyeHeightDefault, game::Camera::kfWaveFadeEndHeight, 0.0f, 1.0f);
	double dSpeedOne = static_cast<double>(std::lerp(gLightingSampledNormalsSpeedOneMin.Get(), gLightingSampledNormalsSpeedOneMax.Get(), fCameraHeightZoomFactor));
	double dSpeedTwo = static_cast<double>(std::lerp(gLightingSampledNormalsSpeedTwoMin.Get(), gLightingSampledNormalsSpeedTwoMax.Get(), fCameraHeightZoomFactor));
	double dSpeedThree = static_cast<double>(std::lerp(gLightingSampledNormalsSpeedThreeMin.Get(), gLightingSampledNormalsSpeedThreeMax.Get(), fCameraHeightZoomFactor));
	// Rotation angles hoisted (shared with the RotatedCamera calls below).
	float fRotationOne = gWaterNormalRotationOne.Get();
	float fRotationTwo = gWaterNormalRotationTwo.Get();
	float fRotationThree = gWaterNormalRotationThree.Get();
	// Per-sample scroll-direction offsets φ (independent of rotation θ; the default 0 scrolls along world (1,1)).
	float fSpeedDirectionOne = gWaterNormalSpeedDirectionOne.Get();
	float fSpeedDirectionTwo = gWaterNormalSpeedDirectionTwo.Get();
	float fSpeedDirectionThree = gWaterNormalSpeedDirectionThree.Get();
	// Per-sample reduced-time accumulators: integrate (size * speed * dt) per frame and fmod 10.0
	// rather than recomputing fmod(size * speed * t, 10.0). Per-frame integration keeps the UV
	// phase continuous when size or speed slide smoothly (e.g. zoom-driven speed lerp); the old
	// formulation produced a per-frame jump proportional to (deltaSpeed * t) that grew with playtime.
	// vec2 form: the scalar delta (size * speed * dt) scrolls along an independent per-sample world
	// direction R(φ)·(1,1) (φ = gWaterNormalSpeedDirection; the default 0 scrolls along world (1,1)), decoupled from the
	// pattern rotation θ. The CPU applies the UV-space direction R(γ) with γ = φ − θ so the shader's
	// R(θ) cancels back to world R(φ); magnitude stays √2 for all φ (direction never alters scroll speed).
	// Each component wraps at 10.0 independently and speedMult * 10 stays integer so fract() absorbs the
	// wrap for any φ (precision contract intact).
	static double sdReducedTimeOneX = 0.0;
	static double sdReducedTimeOneY = 0.0;
	static double sdReducedTimeTwoX = 0.0;
	static double sdReducedTimeTwoY = 0.0;
	static double sdReducedTimeThreeX = 0.0;
	static double sdReducedTimeThreeY = 0.0;
	static float sfPrevElapsedTime = 0.0f;
	float fDeltaTime = std::max(0.0f, rGlobalLayout.fElapsedTime - sfPrevElapsedTime);
	sfPrevElapsedTime = rGlobalLayout.fElapsedTime;
	double dDeltaTime = static_cast<double>(fDeltaTime);
	double dDeltaOne = dSizeBaseOne * dSpeedOne * dDeltaTime;
	double dCosOne = static_cast<double>(std::cos(fRotationOne));
	double dSinOne = static_cast<double>(std::sin(fRotationOne));
	// scroll direction γ = speedDirection − rotation (world dir R(φ)·(1,1); see block comment above)
	double dScrollGammaOne = static_cast<double>(fSpeedDirectionOne) - static_cast<double>(fRotationOne);
	double dScrollCosOne = std::cos(dScrollGammaOne);
	double dScrollSinOne = std::sin(dScrollGammaOne);
	sdReducedTimeOneX = std::fmod(sdReducedTimeOneX + dDeltaOne * (dScrollCosOne - dScrollSinOne), 10.0);
	sdReducedTimeOneY = std::fmod(sdReducedTimeOneY + dDeltaOne * (dScrollCosOne + dScrollSinOne), 10.0);
	double dDeltaTwo = dSizeBaseTwo * dSpeedTwo * dDeltaTime;
	double dCosTwo = static_cast<double>(std::cos(fRotationTwo));
	double dSinTwo = static_cast<double>(std::sin(fRotationTwo));
	double dScrollGammaTwo = static_cast<double>(fSpeedDirectionTwo) - static_cast<double>(fRotationTwo);
	double dScrollCosTwo = std::cos(dScrollGammaTwo);
	double dScrollSinTwo = std::sin(dScrollGammaTwo);
	sdReducedTimeTwoX = std::fmod(sdReducedTimeTwoX + dDeltaTwo * (dScrollCosTwo - dScrollSinTwo), 10.0);
	sdReducedTimeTwoY = std::fmod(sdReducedTimeTwoY + dDeltaTwo * (dScrollCosTwo + dScrollSinTwo), 10.0);
	double dDeltaThree = dSizeBaseThree * dSpeedThree * dDeltaTime;
	double dCosThree = static_cast<double>(std::cos(fRotationThree));
	double dSinThree = static_cast<double>(std::sin(fRotationThree));
	rGlobalLayout.f4WaterNormalRotationOne = {static_cast<float>(dCosOne), -static_cast<float>(dSinOne), static_cast<float>(dSinOne), static_cast<float>(dCosOne)};
	rGlobalLayout.f4WaterNormalRotationTwo = {static_cast<float>(dCosTwo), -static_cast<float>(dSinTwo), static_cast<float>(dSinTwo), static_cast<float>(dCosTwo)};
	rGlobalLayout.f4WaterNormalRotationThree = {static_cast<float>(dCosThree), -static_cast<float>(dSinThree), static_cast<float>(dSinThree), static_cast<float>(dCosThree)};
	double dScrollGammaThree = static_cast<double>(fSpeedDirectionThree) - static_cast<double>(fRotationThree);
	double dScrollCosThree = std::cos(dScrollGammaThree);
	double dScrollSinThree = std::sin(dScrollGammaThree);
	sdReducedTimeThreeX = std::fmod(sdReducedTimeThreeX + dDeltaThree * (dScrollCosThree - dScrollSinThree), 10.0);
	sdReducedTimeThreeY = std::fmod(sdReducedTimeThreeY + dDeltaThree * (dScrollCosThree + dScrollSinThree), 10.0);
	double dCameraX = static_cast<double>(f4CameraPos.x);
	double dCameraY = static_cast<double>(f4CameraPos.y);

	// Per-sample reduced origin: rotate cameraXY on the CPU by the same R(-θ) the shader uses,
	// THEN fmod. This keeps the precision invariant under rotation: the reducedOrigin already
	// encodes the rotation, so the shader-side wrap shift is sizeMult*10 = integer (cleanly
	// absorbed by fract). If we instead let the shader rotate reducedOrigin, the wrap shift
	// becomes R*(sizeMult*10, 0) — non-integer for any θ that isn't a multiple of π/2 — and
	// produces a visible normal-pattern jump every time size*cameraXY crosses a multiple of 10.
	// Takes the precomputed rotation cos/sin (dCos*/dSin*, computed once above) rather than
	// recomputing the same trig per sample. Note the reduced-time accumulation uses its own
	// dScrollCos*/dScrollSin* (of γ = φ − θ), not these rotation-only values.
	auto RotatedCamera = [&](double dCos, double dSin, double& rdOutX, double& rdOutY)
	{
		rdOutX = dCos * dCameraX + dSin * dCameraY;
		rdOutY = -dSin * dCameraX + dCos * dCameraY;
	};
	double dRotCameraXOne = 0.0;
	double dRotCameraYOne = 0.0;
	double dRotCameraXTwo = 0.0;
	double dRotCameraYTwo = 0.0;
	double dRotCameraXThree = 0.0;
	double dRotCameraYThree = 0.0;
	RotatedCamera(dCosOne, dSinOne, dRotCameraXOne, dRotCameraYOne);
	RotatedCamera(dCosTwo, dSinTwo, dRotCameraXTwo, dRotCameraYTwo);
	RotatedCamera(dCosThree, dSinThree, dRotCameraXThree, dRotCameraYThree);

	rGlobalLayout.fWaterReducedNormalOriginX = static_cast<float>(std::fmod(dSizeBaseOne * dRotCameraXOne, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginY = static_cast<float>(std::fmod(dSizeBaseOne * dRotCameraYOne, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeX = static_cast<float>(sdReducedTimeOneX);
	rGlobalLayout.fWaterReducedNormalTimeY = static_cast<float>(sdReducedTimeOneY);
	rGlobalLayout.fWaterReducedNormalOriginTwoX = static_cast<float>(std::fmod(dSizeBaseTwo * dRotCameraXTwo, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginTwoY = static_cast<float>(std::fmod(dSizeBaseTwo * dRotCameraYTwo, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeTwoX = static_cast<float>(sdReducedTimeTwoX);
	rGlobalLayout.fWaterReducedNormalTimeTwoY = static_cast<float>(sdReducedTimeTwoY);
	rGlobalLayout.fWaterReducedNormalOriginThreeX = static_cast<float>(std::fmod(dSizeBaseThree * dRotCameraXThree, 10.0));
	rGlobalLayout.fWaterReducedNormalOriginThreeY = static_cast<float>(std::fmod(dSizeBaseThree * dRotCameraYThree, 10.0));
	rGlobalLayout.fWaterReducedNormalTimeThreeX = static_cast<float>(sdReducedTimeThreeX);
	rGlobalLayout.fWaterReducedNormalTimeThreeY = static_cast<float>(sdReducedTimeThreeY);

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

// Cross-TU ordering contract: RenderFrameGlobal (GlobalUniforms.cpp) calls this exactly once per frame,
// after publishing rGlobalLayout.fElapsedTime — PopulateWaterReducedUv reads that field to derive its
// per-frame delta for the reduced-time accumulators.
void PopulateWaterParameters(shaders::GlobalLayout& rGlobalLayout, float fSunAngle, float fDayPercent)
{
	// Water global
	rGlobalLayout.fWaterEarlyOut = gWaterEarlyOut.Get();
	rGlobalLayout.fWaterHeight = gWaterHeight.Get();
	rGlobalLayout.fWaterTerrainHeight = gWaterTerrainHeight.Get();
	rGlobalLayout.fWaterTerrainFadeInv = 1.0f / gWaterTerrainFade.Get(); // unguarded (reproduces Water.frag's original divide)
	rGlobalLayout.fWaterTerrainFadeClamp = gWaterTerrainFadeClamp.Get();

	rGlobalLayout.fWaterDepthLutFeather = gWaterDepthLutFeather.Get();
	rGlobalLayout.fWaterDepthColorFeather = gWaterDepthColorFeather.Get();
	rGlobalLayout.fWaterDepthColorFloor = gWaterDepthColorFloor.Get();
	rGlobalLayout.fWaterUnderseaCompressionInv = 1.0f / gWaterUnderseaCompression.Get(); // TerrainElevation.frag undersea depth-curve exponent
	rGlobalLayout.fWaterDepthReflectionFeatherInv = 1.0f / (fDayPercent * gWaterDepthReflectionFeather.Get()); // unguarded: night dayPercent=0 -> +inf absorbed by the shader clamp
	rGlobalLayout.fWaterColorNoiseFrequency = gWaterColorNoiseFrequency.Get();

	PopulateWaterSunsetFade(rGlobalLayout, fSunAngle);

	rGlobalLayout.fWaterFresnel = std::pow(fDayPercent, 0.5f) * gWaterFresnel.Get();
	rGlobalLayout.fWaterColorBottom = gWaterColorBottom.Get();
	rGlobalLayout.fWaterColorHeightInv = 1.0f / gWaterColorHeight.Get();
	rGlobalLayout.fWaterColorNoiseAmount = gWaterColorNoiseAmount.Get();
	rGlobalLayout.fWaterColorNoiseWeightOne = gWaterColorNoiseWeightOne.Get();
	rGlobalLayout.fWaterColorNoiseWeightTwo = gWaterColorNoiseWeightTwo.Get();
	rGlobalLayout.fWaterColorNoiseMultiplierOne = gWaterColorNoiseMultiplierOne.Get();
	rGlobalLayout.fWaterColorNoiseMultiplierTwo = gWaterColorNoiseMultiplierTwo.Get();

	PopulateWaterDirectional(rGlobalLayout, fSunAngle);

	const float fBreakStart = gWaterBreakStartDepth.Get();
	const float fBreakEnd = gWaterBreakEndDepth.Get();
	const float fEffectiveBreakStart = std::min(fBreakStart, fBreakEnd);
	const float fBreakRange = fBreakEnd - fEffectiveBreakStart;
	const float fMediumShoreWidth = gWaterMediumShoreSoftness.Get();
	rGlobalLayout.fWaterBreakEndDepthInv = 1.0f / fBreakEnd;
	rGlobalLayout.fWaterBreakBlendStart = fEffectiveBreakStart;
	rGlobalLayout.fWaterBreakBlendInvRange = fBreakRange > 0.0f ? 1.0f / fBreakRange : 0.0f;
	rGlobalLayout.fWaterBreakBlendCurve = gWaterBreakBlendCurve.Get();
	rGlobalLayout.fWaterMediumShoreFadeInvWidth = fMediumShoreWidth > 0.0f ? 1.0f / fMediumShoreWidth : 0.0f;
	rGlobalLayout.fWaterLowSteepness = gWaterLowSteepness.Get();

	rGlobalLayout.fWaterMediumSteepness = gWaterMediumSteepness.Get();
	rGlobalLayout.fWaterWaveNormalBlend = gWaterWaveNormalBlend.Get();
	rGlobalLayout.fWaterLowAmplitude = gWaterLowAmplitude.Get();
	rGlobalLayout.fWaterMediumAmplitude = gWaterMediumAmplitude.Get();
	rGlobalLayout.fWaterZOffsetTemp = gWaterZOffsetTemp.Get(); // DT: TEMP

	rGlobalLayout.iWaterLowCount = static_cast<int>(std::min(gWaterLowCount.Get<int64_t>(), static_cast<int64_t>(gWaterLowMax.Get())));
	rGlobalLayout.iWaterMediumCount = static_cast<int>(gWaterMediumCount.Get<int64_t>());

	PopulateWaterReducedUv(rGlobalLayout);
}

} // namespace engine

#endif // defined(BT_CLIENT)
