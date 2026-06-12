#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Pre-pass that bakes ONLY the first (highest-power) lobe of Water.frag's skybox specular at
// hardcoded 4x MSAA with full sample shading. Output is resolved into the resolve sibling RT and
// sampled in screen space by Water.frag at binding 12. Lets Water itself drop kSampleShading
// because the remaining Two/Three lobes have low-enough power to alias acceptably.
//
// Anything that affects screen-space alignment with the main water draw (vertex shader, geometry,
// camera, wave amplitudes, normal composition) MUST match Water.frag byte-for-byte. The normal
// composition block below is copy-pasted from Water.frag (f3ToEyeNormal through f3SampledNormal)
// for that reason. Any change to either copy must be mirrored in the other.

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

// Binding numbers MUST match the main water pipeline because both pipelines share Water.vert and the
// vertex shader samples elevation at binding 5. See PipelineManager.cpp's kPipelineWaterSkyboxOne block.
layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 1, binding = 6) uniform samplerCube skyboxSampler;
layout (set = 1, binding = 8) uniform sampler2D pWaterNormalSamplers[17];

// Input
layout (location = 0) in vec2 f2InInitialPosition;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InVisibleAreaTexcoord;
layout (location = 3) in vec3 f3InNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

vec3 DecodeNormal(vec2 f2Encoded)
{
	vec2 f2XY = vec2(1.0f - 2.0f * f2Encoded.x, 1.0f - 2.0f * f2Encoded.y);
	return vec3(f2XY, sqrt(clamp(1.0f - dot(f2XY, f2XY), 0.0f, 1.0f)));
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

	// Eye normal — must match Water.frag's f3ToEyeNormal computation exactly so the One-lobe lands on the same per-sample location.
	vec3 f3ToEyeNormal = normalize(vec3(-f2InInitialPosition, mainLayout.f4EyePosition.z));
	f3ToEyeNormal = normalize(mix(f3ToEyeNormal, mainLayout.f4ToEyeNormal.xyz, mainLayout.fLightingWaterSkyboxNormalSoften));

	// Sampled-normal composition — mirrors Water.frag's normal-map sampling block (fSizeOne through
	// f3SampledNormal). Derivatives taken from un-scaled local
	// position so mip selection stays stable across fract() wraps.
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

	vec3 f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 0.2f, 1.1f, vec2(0.1f, 0.2f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 1.1f, 1.2f, vec2(0.2f, 0.3f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexOne], fSizeOne, f2ReducedOrigin, fReducedTime, m2UvRotOne, 2.5f, 1.3f, vec2(0.3f, 0.4f))
	vec3 f3SampledNormalOne = f3Accum;
	f3SampledNormalOne.xy = m2NormalRotOne * f3SampledNormalOne.xy;

	f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 0.3f, 1.4f, vec2(0.4f, 0.5f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 1.2f, 1.5f, vec2(0.6f, 0.7f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexTwo], fSizeTwo, f2ReducedOriginTwo, fReducedTimeTwo, m2UvRotTwo, 3.0f, 1.6f, vec2(0.8f, 0.9f))
	vec3 f3SampledNormalTwo = f3Accum;
	f3SampledNormalTwo.xy = m2NormalRotTwo * f3SampledNormalTwo.xy;

	f3Accum = vec3(0.0f);
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 0.4f, 1.7f, vec2(1.0f, 1.1f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 1.3f, 1.8f, vec2(1.2f, 1.3f))
	SAMPLE_NORMAL_PRECISE(pWaterNormalSamplers[mainLayout.uiWaterNormalIndexThree], fSizeThree, f2ReducedOriginThree, fReducedTimeThree, m2UvRotThree, 3.5f, 1.9f, vec2(1.4f, 1.5f))
	vec3 f3SampledNormalThree = f3Accum;
	f3SampledNormalThree.xy = m2NormalRotThree * f3SampledNormalThree.xy;

	#undef SAMPLE_NORMAL_PRECISE

	float fWeightOne = mix(mainLayout.fWaterNormalWeightOneMin, mainLayout.fWaterNormalWeightOneMax, mainLayout.fCameraHeightZoomFactor);
	float fWeightTwo = mix(mainLayout.fWaterNormalWeightTwoMin, mainLayout.fWaterNormalWeightTwoMax, mainLayout.fCameraHeightZoomFactor);
	float fWeightThree = mix(mainLayout.fWaterNormalWeightThreeMin, mainLayout.fWaterNormalWeightThreeMax, mainLayout.fCameraHeightZoomFactor);
	vec3 f3WeightedSum = fWeightOne * f3SampledNormalOne + fWeightTwo * f3SampledNormalTwo + fWeightThree * f3SampledNormalThree;
	vec3 f3SampledNormal = f3WeightedSum / max(length(f3WeightedSum), kfEpsilon);

	// Sun/moon combined sky-disk color — mirrors Water.frag's f3WaterSun / f3WaterMoon max-combine.
	vec3 f3WaterSun  = globalLayout.fSunIntensityWater  * globalLayout.f4SunColor.xyz;
	vec3 f3WaterMoon = globalLayout.fMoonIntensityWater * globalLayout.f4MoonColor.xyz;
	vec3 f3SunOrMoon = max(f3WaterSun, f3WaterMoon);

	// Skybox sample at the same blended wave-vs-flat normal as Water.frag's f3SkyboxWaveNormal / f3SkyboxColor block.
	const float fSkyboxNormalBlendWave = mainLayout.fLightingWaterSkyboxNormalBlendWave;
	vec3 f3SkyboxWaveNormal = normalize((1.0f - fSkyboxNormalBlendWave) * f3SampledNormal + fSkyboxNormalBlendWave * f3InNormal);
	vec3 f3SkyboxColor = textureLod(skyboxSampler, -reflect(f3ToEyeNormal, f3SkyboxWaveNormal), mainLayout.fLightingWaterSkyboxLod).xyz;
	vec3 f3SkyboxColorSun = f3SkyboxColor * f3SunOrMoon;

	// Terrain-depth feather and biased sun normal — mirrors Water.frag's fReflectionTerrainMultiplier / f3BiasedSunNormal.
	float fReflectionTerrainMultiplier = clamp(-fTerrainElevation / globalLayout.fWaterDepthReflectionFeather, 0.0f, 1.0f);
	vec3 f3BiasedSunNormal = normalize(vec3(0.0f, 0.0f, mainLayout.fLightingWaterSkyboxSunBias) + globalLayout.f4SunMoonNormal.xyz);

	// One-lobe specular only — inlined from Specular() in ShaderFunctions.h. The same `vec3(-1,1,-1) *
	// f3ToEyeNormal` flip is preserved here because the corresponding term is dropped from Water.frag's
	// inlined Two+Three sum.
	vec3 f3LightReflectionNormal = reflect(f3BiasedSunNormal, reflect(f3ToEyeNormal, f3SkyboxWaveNormal));
	float fSpecularFactor = dot(vec3(-1.0f, 1.0f, -1.0f) * f3ToEyeNormal, f3LightReflectionNormal);
	float fOneLobe = fSpecularFactor > 0.0f
		? globalLayout.fLightingWaterSkyboxOne * exp2(mainLayout.fLightingWaterSkyboxOnePower * log2(fSpecularFactor))
		: 0.0f;
	float fReflectionOne = mainLayout.fLightingWaterSkyboxIntensity * fReflectionTerrainMultiplier * fOneLobe;

	// Final composite — matches Water.frag's chain so Water.frag can just add this RT into its
	// inlined Two+Three contribution.
	vec3 f3Out = (1.0f + mainLayout.fLightingWaterSkyboxAdd) * fReflectionOne * f3SkyboxColorSun;
	f4OutColor = vec4(f3Out, 1.0f);
}
