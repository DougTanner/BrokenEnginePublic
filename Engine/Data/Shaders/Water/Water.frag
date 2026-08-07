#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Specular-AA variant select — compile-time, toggled by editing this define and re-running DataPacker
// (same in-source mechanism as DT_LIGHTING_ONLY in ShaderLayoutsBase.h). Filters the sub-pixel high-power
// skybox specular lobes analytically so no MSAA sample-shading (or pre-pass) is needed.
// Runtime sliders: fWaterSpecAAVariance tunes modes 1-3; fWaterSpecAAThreshold modes 2-3.
// 0 = off — pointwise lobes (flickers; baseline reference)
// 1 = scalar-domain analytic box filter — closed-form integral of s^p over the pixel footprint
// 2 = NDF variance widening — Kaplanyan/Tokuyoshi geometric specular AA via the Vlachos production form
// 3 = octave-agreement Toksvig — variance from the multi-sample normal weighted-sum length
// 4 = 2x2 analytic supersample — lobe chain re-evaluated at derivative-extrapolated normals (ALU only)
#define WATER_SPEC_AA_MODE 3

// Zoom/minification handoff for modes 2-3: adds DataPacker-baked per-mip Toksvig variance — the
// normal variance the BC5 mip chain averages away, invisible to the screen-space kernels above
// because DecodeNormal re-unitizes every fetch — into the lobe kernel via an analytic per-octave
// LOD (Bruneton-style geometry->BRDF transition). Fixes camera-zoom specular flicker.
// Runtime sliders: fWaterSpecAAMipScale scales the term; fWaterNormalMipBias biases the analytic
// LOD in lockstep with the water-normal sampler's LOD bias.
#define WATER_SPEC_AA_MIP_HANDOFF 1
// Also hand off the variance the camera-height weight fade removes, referenced to the near-camera
// full-weight look — far water keeps its statistical roughness instead of flattening to gloss.
#define WATER_SPEC_AA_FADE_HANDOFF 0

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (set = 1, binding = 2) uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 3) uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 4) uniform sampler2D objectShadowsTextureSampler;
layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 6) uniform samplerCube skyboxSampler;
layout (set = 1, binding = 7) uniform sampler2D noiseTextureSampler;
layout (set = 1, binding = 8) uniform sampler2D pWaterNormalSamplers[kiWaterNormalCount];
layout (set = 1, binding = 9) uniform sampler2D depthLutSampler;
layout (set = 1, binding = 10) uniform sampler2D smokeSampler;
layout (set = 1, binding = 11) uniform sampler2D ambientLightingSampler;

// Input
layout (location = 0) in vec2 f2InInitialPosition;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InVisibleAreaTexcoord;
layout (location = 3) in vec3 f3InNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

#include "WaterNormalSampling.h"
#include "WaterSpecular.h"
#include "WaterReflectionProjection.h"

void main()
{
	vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(f3InPosition, globalLayout.f4VisibleArea);

	float fTerrainElevation = texture(elevationTextureSampler, f2VisibleAreaTexcoord).x - globalLayout.fWaterHeight;
	if (fTerrainElevation > globalLayout.fWaterEarlyOut)
	{
		f4OutColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
		discard;
		return;
	}

	// Camera-relative to world-space reconstruction
	vec2 f2WaterOrigin = vec2(globalLayout.fWaterOriginX, globalLayout.fWaterOriginY);
	vec2 f2WorldInitialPosition = f2InInitialPosition + f2WaterOrigin;

	// Bias eye normal up a bit for skybox. Compute eye-to-water in camera-relative space rather than
	// reconstructing world position and subtracting f4EyePosition: at large camera coords, the world
	// round-trip drops low bits from f2InInitialPosition (small + large then subtract large), and the
	// lost bits shift with camera motion — visible as a velocity-correlated specular darkening at
	// intermediate fLightingWaterSkyboxNormalSoften. Top-down camera: f4EyePosition.xy == fWaterOrigin
	// by construction (Camera.cpp sets eye directly above target), so eye - waterWorld = -f2InInitial.
	vec3 f3ToEyeNormal = normalize(vec3(-f2InInitialPosition, mainLayout.f4EyePosition.z));
	f3ToEyeNormal = normalize(mix(f3ToEyeNormal, mainLayout.f4ToEyeNormal.xyz, globalLayout.fLightingWaterSkyboxNormalSoften));

	WaterNormalSamplingResult normalResult = SampleWaterNormals();

	// Color (noise with precision-safe UV — same contract as SAMPLE_NORMAL_PRECISE above).
	// CPU stores fmod(freq*camera, 10.0) so mult * 10 must be integer for fract() to absorb the wrap.
	// gWaterColorNoiseMultiplierOne/Two sliders (WaterWrappersBase.cpp) snap to a 0.1 grid so the
	// product stays integer for any tuning (0.0, 0.1, 0.2, ...). Derivatives taken from the un-scaled
	// local position and scaled the same way as the UV keep mip selection stable across the wrap
	// (plain texture() pops at the seam).
	vec2 f2LocalDisplacedPos = f3InPosition.xy - f2WaterOrigin;
	vec2 f2ReducedNoiseOrigin = vec2(globalLayout.fWaterReducedNoiseOriginX, globalLayout.fWaterReducedNoiseOriginY);
	float fMultOne = globalLayout.fWaterColorNoiseMultiplierOne;
	float fMultTwo = globalLayout.fWaterColorNoiseMultiplierTwo;
	vec2 f2NoiseLocalDx = dFdx(f2LocalDisplacedPos);
	vec2 f2NoiseLocalDy = dFdy(f2LocalDisplacedPos);
	float fScaleOne = fMultOne * globalLayout.fWaterColorNoiseFrequency;
	vec2 f2NoiseUvOne = fScaleOne * f2LocalDisplacedPos + fMultOne * f2ReducedNoiseOrigin;
	float fNoiseColorOne = clamp(globalLayout.fWaterColorNoiseWeightOne * globalLayout.fWaterColorNoiseAmount * textureGrad(noiseTextureSampler, fract(f2NoiseUvOne), fScaleOne * f2NoiseLocalDx, fScaleOne * f2NoiseLocalDy).x, -1.0f, 1.0f);
	float fScaleTwo = fMultTwo * globalLayout.fWaterColorNoiseFrequency;
	vec2 f2NoiseUvTwo = fScaleTwo * f2LocalDisplacedPos + fMultTwo * f2ReducedNoiseOrigin;
	float fNoiseColorTwo = clamp(globalLayout.fWaterColorNoiseWeightTwo * globalLayout.fWaterColorNoiseAmount * textureGrad(noiseTextureSampler, fract(f2NoiseUvTwo), fScaleTwo * f2NoiseLocalDx, fScaleTwo * f2NoiseLocalDy).x, -1.0f, 1.0f);
	vec3 f3WaterColor = mix(1.0f * vec3(0.0f, 15.0f / 100.0f, 25.0f / 100.0f), 1.5f * vec3(15.0f / 100.0f, 30.0f / 100.0f, 50.0f / 100.0f), clamp(fNoiseColorOne + fNoiseColorTwo + (f3InPosition.z * globalLayout.fWaterColorHeightInv + globalLayout.fWaterColorBottom), 0.0f, 1.0f));

	vec3 f3DepthColor = texture(depthLutSampler, vec2(globalLayout.fWaterDepthLutFeather * -fTerrainElevation, 0.0f)).xyz;

	float fHeight = f3InPosition.z - fTerrainElevation;
	vec3 f3PreLightingColor = mix(f3DepthColor, f3WaterColor, clamp(fHeight * globalLayout.fWaterDepthColorFeather + globalLayout.fWaterDepthLutSunsetFade, globalLayout.fWaterDepthColorFloor, 1.0f));

	float fDirectionalLighting = max(1.0f - globalLayout.fWaterDirectional, dot(f3InNormal, globalLayout.f4SunMoonNormal.xyz));
	vec3 f3DirectionalLighting = f3PreLightingColor * fDirectionalLighting;
	vec3 f3LightingColor = mix(f3PreLightingColor, f3DirectionalLighting, 0.75f);
	// Per-target water sun/moon: the max-combined color, its /3 sun scalar, and the /3 ambient scalar are
	// folded CPU-side (GlobalUniforms.cpp PopulateDayCycleColors) — all operands are uniform. The prior
	// moon-brightness gating is preserved in the CPU fold (at noon f4MoonColor is ~0). f3SunOrMoon still
	// multiplies the per-pixel skybox color at f3SkyboxColorSun below. Carrying sun and ambient as separate
	// scalars lets fShadowAffectAmbient relax the ambient half only, deferred to the shadow-apply site.
	vec3 f3SunOrMoon = globalLayout.f4WaterSunOrMoon.xyz;
	float fSunScalar     = globalLayout.fWaterSunScalar;
	float fAmbientScalar = globalLayout.fWaterAmbientScalar;

	WaterSpecularResult specularResult = ComposeWaterSpecular(normalResult, f3ToEyeNormal, f3InNormal, f3SunOrMoon, fTerrainElevation);
	vec3 f3SkyboxColor = specularResult.f3SkyboxColor;
	vec3 f3SkyboxColorSun = specularResult.f3SkyboxColorSun;
	float fReflectionTerrainMultiplier = specularResult.fReflectionTerrainMultiplier;
	float fReflection = specularResult.fReflection;

	// Factor the skybox combine so Height Darken can weight base color and skybox specular independently:
	// mix(A, B, t) + add*t*B = (1-t)*A + (1+add)*t*B → base = (1-fReflection)*f3LightingColor, specular = (1+fSkyboxAdd)*fReflection*f3SkyboxColorSun.
	float fSkyboxAdd = mainLayout.fLightingWaterSkyboxAdd;
	vec3 f3SkyboxSpecular = (1.0f + fSkyboxAdd) * fReflection * f3SkyboxColorSun;
	// fReflection is unbounded (~2.5 at defaults at highlight peaks); clamp only this attenuation use so
	// (1-fReflection) can't go negative and drive negative RGB into the F16 target (UNORM used to clamp this).
	f3LightingColor = (1.0f - min(fReflection, 1.0f)) * f3LightingColor;

	// Wave trough darken: at z >= Top no darkening (multiplier 1.0); at z <= Bottom max darkening (multiplier 1.0 - Target).
	// Source/Lighting weights independently mix the per-path multiplier toward 1.0 so each contribution can opt in/out.
	// Lighting also covers the skybox specular and the EWNS lighting deposit (applied at f3WaterLightingMults below).
	float fHeightT = clamp((f3InPosition.z - mainLayout.fWaterHeightDarkenBottom) * mainLayout.fWaterHeightDarkenRangeInv, 0.0f, 1.0f);
	float fHeightDarken = mix(1.0f - mainLayout.fWaterHeightDarkenTarget, 1.0f, fHeightT);
	float fHeightDarkenSource = mix(1.0f, fHeightDarken, mainLayout.fWaterHeightDarkenSource);
	float fHeightDarkenLighting = mix(1.0f, fHeightDarken, mainLayout.fWaterHeightDarkenLighting);
	vec3 f3BaseDarkened           = fHeightDarkenSource   * f3LightingColor;
	vec3 f3SkyboxSpecularDarkened = fHeightDarkenLighting * f3SkyboxSpecular;

	// Shadow with smoke at world position. Moon bypasses the terrain ray-march shadow only;
	// object shadows + smoke volumetric attenuation still apply to both lights. Use a scalar
	// luminance-weighted blend of the two shadow values rather than a per-channel split: the
	// f3SkyboxColor mix above breaks pure linearity in (Sun + Moon), so a per-channel divide
	// would zero entire channels when Sun.c + Moon.c happens to be ~0 (e.g. morning sun has B=0).
	float fShadowMoon = SmokeShadow(globalLayout, f3InPosition, smokeSampler, mainLayout.fSmokeShadowIntensity) * texture(objectShadowsTextureSampler, f2InVisibleAreaTexcoord).x;
	float fShadowSun  = fShadowMoon * SampleTerrainShadow(shadowTextureSampler, WorldToVisibleArea(f3InPosition, globalLayout.f4ShadowArea));
	// Rec.601 sun/moon weights and their guarded reciprocal-sum are folded CPU-side (GlobalUniforms.cpp).
	float fEffectiveShadow = (fShadowSun * globalLayout.fWaterSunWeight + fShadowMoon * globalLayout.fWaterMoonWeight) * globalLayout.fWaterShadowWeightSumInv;
	// fShadowAffectAmbient relaxes shadow on the sky-ambient half only; sun + skybox specular keep full shadow.
	// Mirrors SunLighting() at ShaderFunctions.h:76-80, reusing fEffectiveShadow as its fAmbientShadow (identical Rec.601-weighted formula).
	float fAmbientShadowApplied = mix(1.0f, fEffectiveShadow, globalLayout.fShadowAffectAmbient);
	vec3 f3SunContribution     = fEffectiveShadow      * (fSunScalar     * f3BaseDarkened + f3SkyboxSpecularDarkened);
	vec3 f3AmbientContribution = fAmbientShadowApplied *  fAmbientScalar * f3BaseDarkened;
	f4OutColor.xyz = f3SunContribution + f3AmbientContribution;
	// Cubemap-colored 0.5 ambient floor lifts only darker shadowed water; componentwise max preserves
	// brighter lighting/highlights, while fAmbientShadowApplied keeps Affect Ambient control.
	f4OutColor.xyz = max(f4OutColor.xyz, 0.5f * fAmbientShadowApplied * globalLayout.f4AmbientColor.xyz * f3SkyboxColor);

	// Terrain elevation (for water transparency)
	f4OutColor.w = clamp(-fTerrainElevation * globalLayout.fWaterTerrainFadeInv, globalLayout.fWaterTerrainFadeClamp, 1.0f);

	// Sample lighting texture at projected base-height x/y
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, vec3(f2WorldInitialPosition, 0.0f));
	vec2 f2PositionAtBaseHeightFinal = ProjectWaterReflection(normalResult, f2WorldInitialPosition, f2PositionAtBaseHeight);

	vec2 f2LightingTexcoordBaseHeight = WorldToVisibleArea(vec3(f2PositionAtBaseHeightFinal, 0.0f), globalLayout.f4LightingArea);
	vec4 pf4LightingBaseHeight[3];
	ReadLighting(pf4LightingBaseHeight, pLightingSamplers, f2LightingTexcoordBaseHeight);

	// Ambient sample — straight-down base-height projection, no reflection offset.
	// Uses the precomputed direction-averaged ambient texture (single fetch replaces three EWNS samples).
	vec2 f2LightingTexcoordBaseHeightAmbient = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4LightingArea);
	vec3 f3AmbientSum = ReadAmbientLighting(ambientLightingSampler, f2LightingTexcoordBaseHeightAmbient);

	// Scale base-height lighting (hue-preserving: pow applied to per-direction luminance/average scalar)
	float fWaterEwnsPowMode = mainLayout.fLightingWaterEwnsPowMode;
	// Skip the pow for whichever ratio the mode discards at its extremes (mode is a uniform, so the branch is warp-coherent)
	bool bNeedLumRatio = fWaterEwnsPowMode < 0.999f;
	bool bNeedAvgRatio = fWaterEwnsPowMode > 0.001f;
	for (int i = 0; i < 4; i++)
	{
		vec3 f3Dir = vec3(pf4LightingBaseHeight[0][i], pf4LightingBaseHeight[1][i], pf4LightingBaseHeight[2][i]);
		float fLum = dot(f3Dir, kRec709);
		float fAvg = (f3Dir.x + f3Dir.y + f3Dir.z) / 3.0f;
		float fLumRatio = bNeedLumRatio ? pow(max(fLum, 0.001f), mainLayout.fLightingWaterEwnsPow) / max(fLum, 0.001f) : 0.0f;
		float fAvgRatio = bNeedAvgRatio ? pow(max(fAvg, 0.001f), mainLayout.fLightingWaterEwnsPow) / max(fAvg, 0.001f) : 0.0f;
		float fRatio = mix(fLumRatio, fAvgRatio, fWaterEwnsPowMode);
		pf4LightingBaseHeight[0][i] *= fRatio;
		pf4LightingBaseHeight[1][i] *= fRatio;
		pf4LightingBaseHeight[2][i] *= fRatio;
	}

	// Water lighting
	const float fWaterNormalBlendWave = mainLayout.fLightingWaterNormalBlendWave;
	vec3 f3LightingNormal = (1.0f - fWaterNormalBlendWave) * normalResult.f3SampledNormal + fWaterNormalBlendWave * f3InNormal;
	vec3 f3WaterLighting = WaterLighting(pf4LightingBaseHeight, f3LightingNormal, mainLayout.fLightingWaterNormalSoften, mainLayout.fLightingWaterOne, mainLayout.fLightingWaterOnePower, mainLayout.fLightingWaterTwo, mainLayout.fLightingWaterTwoPower, mainLayout.fLightingWaterThree, mainLayout.fLightingWaterThreePower, mainLayout.fLightingWaterPowerMode);
	float fDepthAttenuation = clamp(-fTerrainElevation * globalLayout.fWaterDepthReflectionFeatherInv, 0.0f, 1.0f);
	vec3 f3WaterLightingScaled = fDepthAttenuation * globalLayout.fLightingTimeOfDayMultiplier * mainLayout.fLightingWaterIntensity * f3WaterLighting;
	// Mix between water-tinted lighting (Add=0) and pure lighting color (Add=1).
	// Total contribution magnitude is conserved across the mix.
	float fWaterLightingAdd = mainLayout.fLightingWaterAdd;
	vec3 f3WaterLightingMults = fHeightDarkenLighting * fReflectionTerrainMultiplier * f3WaterLightingScaled;
	f4OutColor.xyz += (1.0f - fWaterLightingAdd) * f3PreLightingColor * f3WaterLightingMults + fWaterLightingAdd * f3WaterLightingMults;

	// Water ambient (terrain-style, sampled without reflection offset)
	vec3 f3WaterAmbient = globalLayout.fLightingTimeOfDayMultiplier * AmbientLightingPrecomputed(f3AmbientSum, mainLayout.fLightingWaterAmbientIntensity, mainLayout.fLightingWaterAmbientPower, mainLayout.fLightingWaterAmbientPowerMode);
	f4OutColor.xyz += f3WaterAmbient;

	// DT: TEMP — show only lighting texture contributions (with normals and base color)
#ifdef DT_LIGHTING_ONLY
	f4OutColor.xyz = (1.0f - fWaterLightingAdd) * f3PreLightingColor * f3WaterLightingMults + fWaterLightingAdd * f3WaterLightingMults + f3WaterAmbient;
	return;
#endif

	// Additive smoke (precomputed direction-averaged ambient — matches Terrain.frag, decouples smoke from wave normals)
	vec2 f2SmokeTexcoord = WorldToSmokeTexcoord(globalLayout.f4SmokeArea, f2PositionAtBaseHeight);
	float fSmokeRaw = globalLayout.fSmokeMax * texture(smokeSampler, f2SmokeTexcoord).x;
	float fSmokePow = clamp(pow(fSmokeRaw, globalLayout.fSmokePower), 0.0f, 1.0f);
	// The 4.0f un-averages the ambient target: Lighting/LightCombine.comp stores 0.25 * (E+W+N+S) and
	// BlendSmokePrecomputed expects the un-averaged sum. Terrain/Terrain.frag carries the identical scale.
	f4OutColor.xyz = BlendSmokePrecomputed(f4OutColor.xyz, fSmokePow, 4.0f * f3AmbientSum, globalLayout);
}
