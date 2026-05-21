#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (set = 1, binding = 2) uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 3) uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 4) uniform sampler2D objectShadowsTextureSampler;
layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 6) uniform samplerCube skyboxSampler;
layout (set = 1, binding = 7) uniform sampler2D noiseTextureSampler;
layout (set = 1, binding = 8) uniform sampler2D pWaterNormalSamplers[17];
layout (set = 1, binding = 9) uniform sampler2D depthLutSampler;
layout (set = 1, binding = 10) uniform sampler2D smokeSampler;
layout (set = 1, binding = 11) uniform sampler2D ambientLightingSampler;
// Pre-baked One-lobe of the skybox specular (rendered at 4x MSAA + full sample shading and resolved
// in the WaterSkyboxOne pass). Sampled in screen space below to avoid running sample shading here.
layout (set = 1, binding = 12) uniform sampler2D waterSkyboxOneSampler;

// Input
layout (location = 0) in vec2 f2InInitialPosition;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InVisibleAreaTexcoord;
layout (location = 3) in vec3 f3InNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

// BC5 normal map: only XY stored, reconstruct Z = sqrt(1 - X^2 - Y^2). Sign-inverted XY decode is intentional (matches per-island flip convention upstream).
vec3 DecodeNormal(vec2 f2Encoded)
{
	vec2 f2XY = vec2(1.0f - 2.0f * f2Encoded.x, 1.0f - 2.0f * f2Encoded.y);
	return vec3(f2XY, sqrt(clamp(1.0f - dot(f2XY, f2XY), 0.0f, 1.0f)));
}

float Fresnel(vec3 f3CameraPosition, vec3 f3Position, vec3 f3InNormal, float fReduction)
{
	vec3 f3Normal = normalize(f3InNormal);

	// Schlick's approximation fresnel
	float fCosTheta = dot(f3Normal, normalize(f3CameraPosition - f3Position));
	float fF0 = globalLayout.fWaterFresnel;
	float fPow = 1.0f - fCosTheta;
	fPow = fPow * fPow * fPow * fPow; // Note: ^4 instead of ^5
	return clamp(fF0 + (fReduction - fF0) * fPow, 0.0f, 1.0f);
}

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
	f3ToEyeNormal = normalize(mix(f3ToEyeNormal, mainLayout.f4ToEyeNormal.xyz, mainLayout.fLightingWaterSkyboxNormalSoften));

	// Normal map sampling with precision-safe UV computation
	float fSizeOne = mainLayout.fLightingSampledNormalsOneSize;
	float fSizeTwo = mainLayout.fLightingSampledNormalsTwoSize;
	float fSizeThree = mainLayout.fLightingSampledNormalsThreeSize;
	vec2 f2ReducedOrigin = vec2(globalLayout.fWaterReducedNormalOriginX, globalLayout.fWaterReducedNormalOriginY);
	vec2 f2ReducedOriginTwo = vec2(globalLayout.fWaterReducedNormalOriginTwoX, globalLayout.fWaterReducedNormalOriginTwoY);
	vec2 f2ReducedOriginThree = vec2(globalLayout.fWaterReducedNormalOriginThreeX, globalLayout.fWaterReducedNormalOriginThreeY);
	float fReducedTime = globalLayout.fWaterReducedNormalTime;
	float fReducedTimeTwo = globalLayout.fWaterReducedNormalTimeTwo;
	float fReducedTimeThree = globalLayout.fWaterReducedNormalTimeThree;
	vec2 f2LocalDx = dFdx(f2InInitialPosition);
	vec2 f2LocalDy = dFdy(f2InInitialPosition);

	// Per-sample rotation: m2UvRotX = R(-θ) rotates worldUV → texUV (texture pattern appears CCW-rotated by θ in world).
	// m2NormalRotX = R(+θ) is its inverse, applied to the sampled tangent-space normal.xy to bring it back into world frame.
	float fCosOne = cos(mainLayout.fWaterNormalRotationOne);
	float fSinOne = sin(mainLayout.fWaterNormalRotationOne);
	mat2 m2UvRotOne = mat2(fCosOne, -fSinOne, fSinOne, fCosOne);
	mat2 m2NormalRotOne = mat2(fCosOne, fSinOne, -fSinOne, fCosOne);
	float fCosTwo = cos(mainLayout.fWaterNormalRotationTwo);
	float fSinTwo = sin(mainLayout.fWaterNormalRotationTwo);
	mat2 m2UvRotTwo = mat2(fCosTwo, -fSinTwo, fSinTwo, fCosTwo);
	mat2 m2NormalRotTwo = mat2(fCosTwo, fSinTwo, -fSinTwo, fCosTwo);
	float fCosThree = cos(mainLayout.fWaterNormalRotationThree);
	float fSinThree = sin(mainLayout.fWaterNormalRotationThree);
	mat2 m2UvRotThree = mat2(fCosThree, -fSinThree, fSinThree, fCosThree);
	mat2 m2NormalRotThree = mat2(fCosThree, fSinThree, -fSinThree, fCosThree);

	// Note: reducedOrigin is already rotated on the CPU (GlobalUniforms.cpp uses the same per-sample
	// rotation angle to rotate cameraXY before fmod). Rotating it again here would double-rotate
	// AND break precision: the wrap shift sizeMult*10 must be integer for fract() to absorb it,
	// but R*(sizeMult*10, 0) is non-integer for arbitrary θ. So m2UvRot is applied to the
	// camera-relative position and derivatives only — the reducedOrigin stays as-is.
	#define SAMPLE_NORMAL_PRECISE(sampler, size, reducedOrigin, reducedTime, m2UvRot, sizeMult, speedMult, offset) \
	{ \
		float fCallSize = sizeMult * size; \
		vec2 f2UV = offset \
			+ fCallSize * (m2UvRot * f2InInitialPosition) \
			+ sizeMult * reducedOrigin \
			+ speedMult * vec2(reducedTime); \
		vec2 f2Dx = fCallSize * (m2UvRot * f2LocalDx); \
		vec2 f2Dy = fCallSize * (m2UvRot * f2LocalDy); \
		f3Accum += DecodeNormal(textureGrad(sampler, fract(f2UV), f2Dx, f2Dy).rg); \
	}

	// Sample One (3 octaves) — atlas index selected at runtime via uiWaterNormalIndexOne.
	vec3 f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 0.2f, 1.1f, vec2(0.1f, 0.2f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 1.1f, 1.2f, vec2(0.2f, 0.3f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 2.5f, 1.3f, vec2(0.3f, 0.4f))
	vec3 f3SampledNormalOne = f3Accum;
	f3SampledNormalOne.xy = m2NormalRotOne * f3SampledNormalOne.xy;

	// Sample Two (3 octaves)
	f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 0.3f, 1.4f, vec2(0.4f, 0.5f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 1.2f, 1.5f, vec2(0.6f, 0.7f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 3.0f, 1.6f, vec2(0.8f, 0.9f))
	vec3 f3SampledNormalTwo = f3Accum;
	f3SampledNormalTwo.xy = m2NormalRotTwo * f3SampledNormalTwo.xy;

	// Sample Three (3 octaves) — extends the One/Two octave pattern linearly.
	f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 0.4f, 1.7f, vec2(1.0f, 1.1f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 1.3f, 1.8f, vec2(1.2f, 1.3f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 3.5f, 1.9f, vec2(1.4f, 1.5f))
	vec3 f3SampledNormalThree = f3Accum;
	f3SampledNormalThree.xy = m2NormalRotThree * f3SampledNormalThree.xy;

	#undef SAMPLE_NORMAL_PRECISE

	// Per-sample weights interpolated by camera zoom (0 = closest, 1 = farthest), then weighted-sum-then-normalize.
	// normalize() is scale-invariant so absolute weight magnitudes don't matter; only ratios do. Weight 0 disables a sample.
	float fWeightOne = mix(mainLayout.fWaterNormalWeightOneMin, mainLayout.fWaterNormalWeightOneMax, mainLayout.fCameraHeightZoomFactor);
	float fWeightTwo = mix(mainLayout.fWaterNormalWeightTwoMin, mainLayout.fWaterNormalWeightTwoMax, mainLayout.fCameraHeightZoomFactor);
	float fWeightThree = mix(mainLayout.fWaterNormalWeightThreeMin, mainLayout.fWaterNormalWeightThreeMax, mainLayout.fCameraHeightZoomFactor);
	// Guard against NaN: if all three weight sliders resolve to 0 the sum is the zero vector and normalize() returns NaN.
	vec3 f3WeightedSum = fWeightOne * f3SampledNormalOne + fWeightTwo * f3SampledNormalTwo + fWeightThree * f3SampledNormalThree;
	vec3 f3SampledNormal = f3WeightedSum / max(length(f3WeightedSum), kfEpsilon);

	// Color (noise with precision-safe UV — same pact as SAMPLE_NORMAL_PRECISE above).
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
	// Per-target water sun/moon intensity scales the sun and moon contributions before the max-combine.
	// Folds in the previous moon-brightness gating: at noon f4MoonColor is ~0 (moonrise envelope) so any moon multiplier yields 0; at night the multiplier dominates.
	vec3 f3WaterSun  = globalLayout.fSunIntensityWater  * globalLayout.f4SunColor.xyz;
	vec3 f3WaterMoon = globalLayout.fMoonIntensityWater * globalLayout.f4MoonColor.xyz;
	vec3 f3SunOrMoon = max(f3WaterSun, f3WaterMoon);
	// Carry sun and ambient energy as separate scalars so fShadowAffectAmbient can apply the
	// SunLighting() ambient-shadow relaxation (ShaderFunctions.h:76-80) to the f4AmbientColor
	// half only; the multiply into f3LightingColor is deferred to the shadow-apply site.
	float fSunScalar     = (f3SunOrMoon.x + f3SunOrMoon.y + f3SunOrMoon.z) / 3.0f;
	float fAmbientScalar = (globalLayout.f4AmbientColor.x + globalLayout.f4AmbientColor.y + globalLayout.f4AmbientColor.z) / 3.0f;

	// Skybox. The Ryfjallet prefiltered cubemap bound here (kPrefilteredWaterCrc) is oriented to
	// match engine Z-up, so the reflection vector is sampled directly with no Y-up swizzle.
	const float fSkyboxNormalBlendWave = mainLayout.fLightingWaterSkyboxNormalBlendWave;
	vec3 f3SkyboxWaveNormal = normalize((1.0f - fSkyboxNormalBlendWave) * f3SampledNormal + fSkyboxNormalBlendWave * f3InNormal);
	vec3 f3SkyboxColor = textureLod(skyboxSampler, -reflect(f3ToEyeNormal, f3SkyboxWaveNormal), mainLayout.fLightingWaterSkyboxLod).xyz;
	vec3 f3SkyboxColorSun = f3SkyboxColor * f3SunOrMoon;

	float fReflectionTerrainMultiplier = clamp(-fTerrainElevation / globalLayout.fWaterDepthReflectionFeather, 0.0f, 1.0f);
	vec3 f3BiasedSunNormal = normalize(vec3(0.0f, 0.0f, mainLayout.fLightingWaterSkyboxSunBias) + globalLayout.f4SunMoonNormal.xyz);

	// Specular() is inlined here so the high-power One lobe can be extracted into kPipelineWaterSkyboxOne
	// (a 4x-MSAA + full-sample-shading pre-pass) and sampled back in screen space below — letting this
	// pipeline drop kSampleShading. The Two/Three lobes have low enough power to alias acceptably without
	// sample shading and stay inline here.
	vec3 f3LightReflectionNormal = reflect(f3BiasedSunNormal, reflect(f3ToEyeNormal, f3SkyboxWaveNormal));
	float fSpecularFactor = dot(vec3(-1.0f, 1.0f, -1.0f) * f3ToEyeNormal, f3LightReflectionNormal);
	float fSpecularSum = 0.0f;
	if (fSpecularFactor > 0.0f)
	{
		float fSpecularLog2 = log2(fSpecularFactor);
		fSpecularSum =
			mainLayout.fLightingWaterSkyboxTwo   * exp2(mainLayout.fLightingWaterSkyboxTwoPower   * fSpecularLog2) +
			mainLayout.fLightingWaterSkyboxThree * exp2(mainLayout.fLightingWaterSkyboxThreePower * fSpecularLog2);
	}
	float fReflection = mainLayout.fLightingWaterSkyboxIntensity * fReflectionTerrainMultiplier * fSpecularSum;

	// Factor the skybox combine so Height Darken can weight base color and skybox specular independently:
	// mix(A, B, t) + add*t*B = (1-t)*A + (1+add)*t*B → base = (1-fReflection)*f3LightingColor, specular = (1+fSkyboxAdd)*fReflection*f3SkyboxColorSun.
	// The base-color darken now subtracts only Two+Three lobe energy; the One lobe is purely additive from
	// the resolved RT (negligible hue shift in practice — One values are well below 1.0 after all multipliers).
	float fSkyboxAdd = mainLayout.fLightingWaterSkyboxAdd;
	vec3 f3SkyboxSpecular = (1.0f + fSkyboxAdd) * fReflection * f3SkyboxColorSun;
	// One-lobe contribution baked at full sample-shading and resolved — same chain
	// (1+fSkyboxAdd) * fReflection_one * f3SkyboxColorSun already applied inside the pre-pass.
	vec2 f2SkyboxOneUv = gl_FragCoord.xy * mainLayout.f2InvFramebufferSize;
	f3SkyboxSpecular += texture(waterSkyboxOneSampler, f2SkyboxOneUv).rgb;
	f3LightingColor = (1.0f - fReflection) * f3LightingColor;

	// Wave trough darken: at z >= Top no darkening (multiplier 1.0); at z <= Bottom max darkening (multiplier 1.0 - Target).
	// Source/Lighting weights independently mix the per-path multiplier toward 1.0 so each contribution can opt in/out.
	// Lighting also covers the skybox specular and the EWNS lighting deposit (applied at f3WaterLightingMults below).
	float fHeightT = clamp((f3InPosition.z - mainLayout.fWaterHeightDarkenBottom) / (mainLayout.fWaterHeightDarkenTop - mainLayout.fWaterHeightDarkenBottom), 0.0f, 1.0f);
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
	float fShadowSun  = fShadowMoon * texture(shadowTextureSampler, WorldToVisibleArea(f3InPosition, globalLayout.f4ShadowArea)).x;
	float fSunWeight  = dot(f3WaterSun,  vec3(0.299f, 0.587f, 0.114f));
	float fMoonWeight = dot(f3WaterMoon, vec3(0.299f, 0.587f, 0.114f));
	float fEffectiveShadow = (fShadowSun * fSunWeight + fShadowMoon * fMoonWeight) / max(0.001f, fSunWeight + fMoonWeight);
	// fShadowAffectAmbient relaxes shadow on the sky-ambient half only; sun + skybox specular keep full shadow.
	// Mirrors SunLighting() at ShaderFunctions.h:76-80, reusing fEffectiveShadow as its fAmbientShadow (identical Rec.601-weighted formula).
	float fAmbientShadowApplied = mix(1.0f, fEffectiveShadow, globalLayout.fShadowAffectAmbient);
	vec3 f3SunContribution     = fEffectiveShadow      * (fSunScalar     * f3BaseDarkened + f3SkyboxSpecularDarkened);
	vec3 f3AmbientContribution = fAmbientShadowApplied *  fAmbientScalar * f3BaseDarkened;
	f4OutColor.xyz = f3SunContribution + f3AmbientContribution;

	// Terrain elevation (for water transparency)
	f4OutColor.w = clamp(-fTerrainElevation / globalLayout.fWaterTerrainFade, globalLayout.fWaterTerrainFadeClamp, 1.0f);

	// Sample lighting texture at projected base-height x/y
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, vec3(f2WorldInitialPosition, 0.0f));

	// Reflected base-height sample: reflect the eye ray about the water normal and
	// project the reflected ray to fBaseHeight. Distortion scales the normal's XY
	// before renormalization so wave tilt (not the slow eye-to-point gradient) is
	// the dominant contributor to the reflected sample position.
	vec3 f3ReflectedNormal = mix(f3SampledNormal, f3InNormal, mainLayout.fLightingWaterReflectedNormalBlendWave);
	f3ReflectedNormal = normalize(vec3(f3ReflectedNormal.xy * mainLayout.fLightingWaterReflectedDistortion, f3ReflectedNormal.z));
	vec3 f3WaterWorld = vec3(f2WorldInitialPosition, 0.0f);
	vec3 f3EyeToPoint = normalize(f3WaterWorld - mainLayout.f4EyePosition.xyz);
	vec3 f3ReflectedRay = reflect(f3EyeToPoint, f3ReflectedNormal);
	// Guard grazing-normal divide: clamp z away from zero so fReflectedMult can't overflow to +Inf
	float fReflectedMult = (globalLayout.fBaseHeight - f3WaterWorld.z) / max(f3ReflectedRay.z, 1e-4f);
	vec2 f2PositionAtBaseHeightReflected = (f3WaterWorld + max(fReflectedMult, 0.0f) * f3ReflectedRay).xy;

	// Power-curve compression on the XY offset above FalloffStart so heavily-bent
	// normals don't sample hundreds of world units away. Power=1 is passthrough.
	vec2 f2ReflectedOffset = f2PositionAtBaseHeightReflected - f3WaterWorld.xy;
	float fOffsetDistance = length(f2ReflectedOffset);
	float fFalloffStart = mainLayout.fLightingWaterReflectedFalloffStart;
	if (fOffsetDistance > fFalloffStart)
	{
		float fNewDistance = fFalloffStart + pow(fOffsetDistance - fFalloffStart, mainLayout.fLightingWaterReflectedFalloffPower);
		f2PositionAtBaseHeightReflected = f3WaterWorld.xy + f2ReflectedOffset * (fNewDistance / fOffsetDistance);
		fOffsetDistance = fNewDistance;
	}

	float fReflectedFresnel = mix(1.0f, Fresnel(mainLayout.f4EyePosition.xyz, f3WaterWorld, f3ReflectedNormal, 1.0f), mainLayout.fLightingWaterReflectedFresnel);
	float fReflectedAmount = clamp(mainLayout.fLightingWaterReflectedAmount * mainLayout.fLightingWaterReflectedIntensity * fReflectedFresnel, 0.0f, 1.0f);
	vec2 f2PositionAtBaseHeightFinal = mix(f2PositionAtBaseHeight, f2PositionAtBaseHeightReflected, fReflectedAmount);

	vec2 f2LightingTexcoordBaseHeight = WorldToVisibleArea(vec3(f2PositionAtBaseHeightFinal, 0.0f), globalLayout.f4LightingArea);
	vec4 pf4LightingBaseHeight[3];
	ReadLighting(pf4LightingBaseHeight, pLightingSamplers, f2LightingTexcoordBaseHeight);

	// Ambient sample — straight-down base-height projection, no reflection offset.
	// Uses the precomputed direction-averaged ambient texture (single fetch replaces three EWNS samples).
	vec2 f2LightingTexcoordBaseHeightAmbient = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4LightingArea);
	vec3 f3AmbientSum = texture(ambientLightingSampler, f2LightingTexcoordBaseHeightAmbient).xyz;

	// Scale base-height lighting (hue-preserving: pow applied to per-direction luminance/average scalar)
	float fWaterEwnsPowMode = mainLayout.fLightingWaterEwnsPowMode;
	for (int i = 0; i < 4; i++)
	{
		vec3 f3Dir = vec3(pf4LightingBaseHeight[0][i], pf4LightingBaseHeight[1][i], pf4LightingBaseHeight[2][i]);
		float fLum = dot(f3Dir, kRec709);
		float fLumRatio = pow(max(fLum, 0.001f), mainLayout.fLightingWaterEwnsPow) / max(fLum, 0.001f);
		float fAvg = (f3Dir.x + f3Dir.y + f3Dir.z) / 3.0f;
		float fAvgRatio = pow(max(fAvg, 0.001f), mainLayout.fLightingWaterEwnsPow) / max(fAvg, 0.001f);
		float fRatio = mix(fLumRatio, fAvgRatio, fWaterEwnsPowMode);
		pf4LightingBaseHeight[0][i] *= fRatio;
		pf4LightingBaseHeight[1][i] *= fRatio;
		pf4LightingBaseHeight[2][i] *= fRatio;
	}

	// Water lighting
	const float fWaterNormalBlendWave = mainLayout.fLightingWaterNormalBlendWave;
	vec3 f3LightingNormal = (1.0f - fWaterNormalBlendWave) * f3SampledNormal + fWaterNormalBlendWave * f3InNormal;
	vec3 f3WaterLighting = WaterLighting(pf4LightingBaseHeight, f3LightingNormal, mainLayout.fLightingWaterNormalSoften, mainLayout.fLightingWaterOne, mainLayout.fLightingWaterOnePower, mainLayout.fLightingWaterTwo, mainLayout.fLightingWaterTwoPower, mainLayout.fLightingWaterThree, mainLayout.fLightingWaterThreePower, mainLayout.fLightingWaterPowerMode);
	float fDepthAttenuation = clamp(-fTerrainElevation / globalLayout.fWaterDepthReflectionFeather, 0.0f, 1.0f);
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
	f4OutColor.xyz = BlendSmokePrecomputed(f4OutColor.xyz, fSmokePow, 4.0f * f3AmbientSum, globalLayout);
}
