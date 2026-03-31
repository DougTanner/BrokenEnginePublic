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
layout (set = 1, binding = 8) uniform sampler2D normalmapOneTextureSampler;
layout (set = 1, binding = 9) uniform sampler2D normalmapTwoTextureSampler;
layout (set = 1, binding = 10) uniform sampler2D depthLutSampler;
layout (set = 1, binding = 11) uniform sampler2D smokeSampler;

// Input
layout (location = 0) in vec2 f2InInitialPosition;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InVisibleAreaTexcoord;
layout (location = 3) in vec3 f3InNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

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
		return;
	}

	// Bias eye normal up a bit for skybox
	vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - vec3(f2InInitialPosition, 0.0f));
	f3ToEyeNormal = normalize(mix(f3ToEyeNormal, mainLayout.f4ToEyeNormal.xyz, mainLayout.fLightingWaterSkyboxNormalSoften));

	float fSize = mainLayout.fLightingSampledNormalsSize + mainLayout.fLightingSampledNormalsSizeMod * f3InPosition.z;
	float fSpeed = mainLayout.fLightingSampledNormalsSpeed;
	vec3 f3SampledNormalOne = SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 0.2f * fSize, 1.1f * fSize * fSpeed, vec2(0.1f, 0.2f)) +
	                          SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 1.1f * fSize, 1.2f * fSize * fSpeed, vec2(0.2f, 0.3f)) +
	                          SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 2.5f * fSize, 1.3f * fSize * fSpeed, vec2(0.3f, 0.4f));
	vec3 f3SampledNormalTwo = SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 0.3f * fSize, 1.4f * fSize * fSpeed, vec2(0.4f, 0.5f)) +
	                          SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 1.2f * fSize, 1.5f * fSize * fSpeed, vec2(0.6f, 0.7f)) +
	                          SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 3.0f * fSize, 1.6f * fSize * fSpeed, vec2(0.8f, 0.9f));
	vec3 f3SampledNormal = normalize(f3SampledNormalOne + f3SampledNormalTwo);

	// Color
	float fNoiseColorOne = clamp(globalLayout.fWaterColorNoiseAmount * texture(noiseTextureSampler, 2.0f * globalLayout.fWaterColorNoiseFrequency * f3InPosition.xy).x, 0.0f, 1.0f);
	float fNoiseColorTwo = clamp(globalLayout.fWaterColorNoiseAmount * texture(noiseTextureSampler, globalLayout.fWaterColorNoiseFrequency * -f3InPosition.xy).x, 0.0f, 1.0f);
	vec3 f3WaterColor = mix(1.0f * vec3(0.0f, 15.0f / 100.0f, 25.0f / 100.0f), 1.5f * vec3(15.0f / 100.0f, 30.0f / 100.0f, 50.0f / 100.0f), clamp(fNoiseColorOne - fNoiseColorTwo + (f3InPosition.z * globalLayout.fWaterColorHeightInv + globalLayout.fWaterColorBottom), 0.0f, 1.0f));

	vec3 f3DepthColor = texture(depthLutSampler, vec2(globalLayout.fWaterDepthLutFeather * -fTerrainElevation, 0.0f)).xyz;

	float fHeight = f3InPosition.z - fTerrainElevation;
	vec3 f3PreLightingColor = mix(f3DepthColor, f3WaterColor, clamp(fHeight * globalLayout.fWaterDepthColorFeather + globalLayout.fWaterSunVisibility, 0.2f, 1.0f));

	float fDirectionalLighting = max(1.0f - globalLayout.fWaterDirectional, dot(f3InNormal, globalLayout.f4SunMoonNormal.xyz));
	vec3 f3DirectionalLighting = f3PreLightingColor * max(fDirectionalLighting, 0.3f);
	vec3 f3LightingColor = mix(f3PreLightingColor, f3DirectionalLighting, 0.75f);
	vec3 f3Sunlight = globalLayout.f4SunMoonColor.xyz + globalLayout.f4AmbientColor.xyz;
	float fSunlight = (f3Sunlight.x + f3Sunlight.y + f3Sunlight.z) / 3.0f;
	f3LightingColor *= fSunlight;

	// Skybox
	const float fSkyboxNormalBlendWave = mainLayout.fLightingWaterSkyboxNormalBlendWave;
	vec3 f3SkyboxWaveNormal = normalize((1.0f - fSkyboxNormalBlendWave) * f3SampledNormal + fSkyboxNormalBlendWave * f3InNormal);
	vec3 f3SkyboxColor = textureLod(skyboxSampler, -normalize(reflect(f3ToEyeNormal, f3SkyboxWaveNormal)), mainLayout.fLightingWaterSkyboxLod).xyz;
	vec3 f3SkyboxColorSun = f3SkyboxColor * globalLayout.f4SunMoonColor.xyz;

	float fReferenceHeight = 0.05f;
	float fReflectionHeightMultiplier = clamp((f3InPosition.z + fReferenceHeight) / (2.0f * fReferenceHeight), 0.5f, 1.0f);
	float fReflectionTerrainMultiplier = fReflectionHeightMultiplier * clamp(-fTerrainElevation / globalLayout.fWaterDepthReflectionFeather, 0.0f, 1.0f);
	vec3 f3BiasedSunNormal = normalize(vec3(0.0f, 0.0f, mainLayout.fLightingWaterSkyboxSunBias) + globalLayout.f4SunMoonNormal.xyz);
	float fReflection = mainLayout.fLightingWaterSkyboxIntensity * fReflectionTerrainMultiplier * Specular(vec3(-1.0f, 1.0f, -1.0f) * f3ToEyeNormal, f3BiasedSunNormal, normalize(reflect(f3ToEyeNormal, f3SkyboxWaveNormal)), globalLayout.fLightingWaterSkyboxOne, mainLayout.fLightingWaterSkyboxOnePower, mainLayout.fLightingWaterSkyboxTwo, mainLayout.fLightingWaterSkyboxTwoPower, mainLayout.fLightingWaterSkyboxThree, mainLayout.fLightingWaterSkyboxThreePower);

	float fSkyboxAdd = mainLayout.fLightingWaterSkyboxAdd;
	f3LightingColor = mix(f3LightingColor, f3SkyboxColorSun, fReflection);
	f3LightingColor += fSkyboxAdd * fReflection * f3SkyboxColorSun;

	float fReflectionHeightMultiplier2 = clamp((f3InPosition.z - mainLayout.fWaterHeightDarkenBottom) / (mainLayout.fWaterHeightDarkenTop - mainLayout.fWaterHeightDarkenBottom), mainLayout.fWaterHeightDarkenClamp, 1.0f);
	f3LightingColor *= fReflectionHeightMultiplier2;
	f3LightingColor *= fSunlight;

	// Shadow with smoke at world position
	float fShadow = SmokeShadow(globalLayout, f3InPosition, smokeSampler, mainLayout.fSmokeShadowIntensity) * max(0.2f, texture(shadowTextureSampler, f2InVisibleAreaTexcoord).x) * texture(objectShadowsTextureSampler, f2InVisibleAreaTexcoord).x;
	f4OutColor.xyz = fShadow * f3LightingColor;
	f4OutColor.xyz = max(f4OutColor.xyz, 0.5f * globalLayout.f4AmbientColor.xyz * f3SkyboxColor);

	// Terrain elevation (for water transparency)
	f4OutColor.w = clamp(-fTerrainElevation / globalLayout.fWaterTerrainFade, globalLayout.fWaterTerrainFadeClamp, 1.0f);

	// Sample lighting texture at projected base-height x/y
	vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, vec3(f2InInitialPosition, 0.0f));
	vec2 f2LightingTexcoordBaseHeight = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4LightingArea);
	vec4 pf4LightingBaseHeight[3] = {texture(pLightingSamplers[0], f2LightingTexcoordBaseHeight), texture(pLightingSamplers[1], f2LightingTexcoordBaseHeight), texture(pLightingSamplers[2], f2LightingTexcoordBaseHeight)};

	// Scale base-height lighting
	pf4LightingBaseHeight[0] = globalLayout.fLightingTimeOfDayMultiplier * pow(pf4LightingBaseHeight[0], vec4(mainLayout.fLightingNewAmbientPower));
	pf4LightingBaseHeight[1] = globalLayout.fLightingTimeOfDayMultiplier * pow(pf4LightingBaseHeight[1], vec4(mainLayout.fLightingNewAmbientPower));
	pf4LightingBaseHeight[2] = globalLayout.fLightingTimeOfDayMultiplier * pow(pf4LightingBaseHeight[2], vec4(mainLayout.fLightingNewAmbientPower));

	// Water lighting
	const float fWaterNormalBlendWave = mainLayout.fLightingWaterNormalBlendWave;
	vec3 f3LightingNormal = (1.0f - fWaterNormalBlendWave) * f3SampledNormal + fWaterNormalBlendWave * f3InNormal;
	vec3 f3WaterLighting = WaterLighting(pf4LightingBaseHeight, f3LightingNormal, mainLayout.fLightingWaterNormalSoften, mainLayout.fLightingWaterOne, mainLayout.fLightingWaterOnePower, mainLayout.fLightingWaterTwo, mainLayout.fLightingWaterTwoPower, mainLayout.fLightingWaterThree, mainLayout.fLightingWaterThreePower);
	float fDepthAttenuation = clamp(-fTerrainElevation / globalLayout.fWaterDepthReflectionFeather, 0.0f, 1.0f);
	vec3 f3WaterLightingScaled = fDepthAttenuation * mainLayout.fLightingWaterIntensity * f3WaterLighting;
	float fWaterLightingAdd = mainLayout.fLightingWaterAdd;
	vec3 f3WaterLightingColor = f3WaterLightingScaled * mix(f3PreLightingColor, vec3(1.0f), fWaterLightingAdd);
	f4OutColor.xyz += fReflectionHeightMultiplier2 * fReflectionTerrainMultiplier * f3WaterLightingColor;

	// Additive smoke
	vec2 f2SmokeTexcoord = WorldToSmokeTexcoord(globalLayout.f4SmokeArea, f2PositionAtBaseHeight);
	float fSmokeRaw = globalLayout.fSmokeMax * texture(smokeSampler, f2SmokeTexcoord).x;
	float fSmokePow = clamp(pow(fSmokeRaw, globalLayout.fSmokePower), 0.0f, 1.0f);
	f4OutColor.xyz = BlendSmoke(f4OutColor.xyz, fSmokePow, pf4LightingBaseHeight, globalLayout);
}
