#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (binding = 2) uniform sampler2D pLightingSamplers[3];
layout (binding = 3) uniform sampler2D shadowTextureSampler;
layout (binding = 4) uniform sampler2D objectShadowsTextureSampler;
layout (binding = 5) uniform sampler2D elevationTextureSampler;
layout (binding = 6) uniform samplerCube skyboxSampler;
layout (binding = 7) uniform sampler2D noiseTextureSampler;
layout (binding = 8) uniform sampler2D normalmapOneTextureSampler;
layout (binding = 9) uniform sampler2D normalmapTwoTextureSampler;
layout (binding = 10) uniform sampler2D depthLutSampler;
layout (binding = 11) uniform sampler2D smokeSampler;

// Input
layout (location = 0) in vec2 f2InInitialPosition;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InVisibleAreaTexcoord;
layout (location = 3) in vec3 f3InNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

float FresnelSchlickRatio(float fCosIncident)
{
	float p = 1.0f - fCosIncident;
	float p2 = p * p;
	return p2 * p2 * p;
}

float Fresnel(vec3 cameraPos, vec3 position, vec3 normal, float fReduction)
{
    // normal.x = 0.75f * normal.x;
    normal = normalize(normal);

    // Schlick's approximation fresnel
    float cosTheta = dot(normal, normalize(cameraPos - position));
	float F0 = globalLayout.f4WaterFour.x;
    float fPow = 1.0f - cosTheta;
    fPow = fPow * fPow * fPow * fPow; // Note: ^3 instead of ^5
    return clamp(F0 + (fReduction - F0) * fPow, 0.0f, 1.0f);
}

void main()
{
    vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(f3InPosition, globalLayout.f4VisibleArea);

	float fTerrainElevation = texture(elevationTextureSampler, f2VisibleAreaTexcoord).x;
	if (fTerrainElevation > globalLayout.f4Terrain.w)
	{
		f4OutColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}

    // Bias eye normal up a bit for skybox
    vec3 f3ToEyeNormal = normalize(mainLayout.f4EyePosition.xyz - vec3(f2InInitialPosition, 0.0f));
    f3ToEyeNormal = normalize(mix(f3ToEyeNormal, mainLayout.f4ToEyeNormal.xyz, mainLayout.fLightingWaterSkyboxNormalSoften));

    float fSize = mainLayout.fLightingSampledNormalsSize + mainLayout.fLightingSampledNormalsSizeMod * f3InPosition.z;
    float fSpeed = mainLayout.fLightingSampledNormalsSpeed;
    vec3 f3SampledNormalOne = SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 0.2f * fSize, 1.1f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.1f, 0.2f)) +
                              SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 1.1f * fSize, 1.2f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.2f, 0.3f)) +
                              SampleNormal(globalLayout, normalmapOneTextureSampler, f2InInitialPosition, 2.5f * fSize, 1.3f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.3f, 0.4f));
    vec3 f3SampledNormalTwo = SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 0.3f * fSize, 1.4f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.4f, 0.5f)) +
                              SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 1.2f * fSize, 1.5f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.6f, 0.7f)) +
                              SampleNormal(globalLayout, normalmapTwoTextureSampler, f2InInitialPosition, 3.0f * fSize, 1.6f * fSize * fSpeed, globalLayout.f4Misc.x, vec2(0.8f, 0.9f));
    vec3 f3SampledNormal = normalize(f3SampledNormalOne + f3SampledNormalTwo);

    // Color
    float fNoiseColorOne = clamp(globalLayout.f4WaterFour.w * texture(noiseTextureSampler, 2.0f * globalLayout.f4WaterTwo.w * f3InPosition.xy).x, 0.0f, 1.0f);
    float fNoiseColorTwo = clamp(globalLayout.f4WaterFour.w * texture(noiseTextureSampler, globalLayout.f4WaterTwo.w * -f3InPosition.xy).x, 0.0f, 1.0f);
    vec3 f3WaterColor = mix(1.0f * vec3(0.0f, 15.0f / 100.0f, 25.0f / 100.0f), 1.5f * vec3(15.0f / 100.0f, 30.0f / 100.0f, 50.0f / 100.0f), clamp(fNoiseColorOne - fNoiseColorTwo + (f3InPosition.z * globalLayout.f4WaterFour.z + globalLayout.f4WaterFour.y), 0.0f, 1.0f));

    vec3 f3DepthColor = texture(depthLutSampler, vec2(globalLayout.f4WaterTwo.x * -fTerrainElevation, 0.0f)).xyz;

    float fHeight = f3InPosition.z - fTerrainElevation;
    vec3 f3PreLightingColor = mix(f3DepthColor, f3WaterColor, clamp(fHeight * globalLayout.f4WaterTwo.y + globalLayout.f4WaterThree.w, 0.2f, 1.0f));

    float fDirectionalLighting = max(1.0f - globalLayout.f4WaterFive.w, dot(f3InNormal, globalLayout.f4SunNormal.xyz));
    vec3 f3DirectionalLighting = f3PreLightingColor * max(fDirectionalLighting, 0.3f);
    vec3 f3LightingColor = mix(f3PreLightingColor, f3DirectionalLighting, 0.75f);
    vec3 f3Sunlight = globalLayout.f4SunColor.xyz + globalLayout.f4AmbientColor.xyz;
    float fSunlight = (f3Sunlight.x + f3Sunlight.y + f3Sunlight.z) / 3.0f;
    f3LightingColor *= fSunlight;

    // Skybox
    const float fSkyboxNormalBlendWave = mainLayout.fLightingWaterSkyboxNormalBlendWave;
	vec3 f3SkyboxWaveNormal = normalize((1.0f - fSkyboxNormalBlendWave) * f3SampledNormal + fSkyboxNormalBlendWave * f3InNormal);
    vec3 f3SkyboxColor = texture(skyboxSampler, -normalize(reflect(f3ToEyeNormal, f3SkyboxWaveNormal))).xyz;
    vec3 f3SkyboxColorSun = f3SkyboxColor * globalLayout.f4SunColor.xyz;

    float fReferenceHeight = 0.05f;
    float fReflectionHeightMultiplier = clamp((f3InPosition.z + fReferenceHeight) / (2.0f * fReferenceHeight), 0.5f, 1.0f);
    float fReflectionTerrainMultiplier = fReflectionHeightMultiplier * clamp(-fTerrainElevation / globalLayout.f4WaterTwo.z, 0.0f, 1.0f);
    vec3 f3BiasedSunNormal = normalize(vec3(0.0f, 0.0f, mainLayout.fLightingWaterSkyboxSunBias) + globalLayout.f4SunNormal.xyz);
    float fReflection = mainLayout.fLightingWaterSkyboxIntensity * fReflectionTerrainMultiplier * Specular(vec3(-1.0f, 1.0f, -1.0f) * f3ToEyeNormal, f3BiasedSunNormal, normalize(reflect(f3ToEyeNormal, f3SkyboxWaveNormal)), mainLayout.fLightingWaterSkyboxOne, mainLayout.fLightingWaterSkyboxOnePower, mainLayout.fLightingWaterSkyboxTwo, mainLayout.fLightingWaterSkyboxTwoPower, mainLayout.fLightingWaterSkyboxThree, mainLayout.fLightingWaterSkyboxThreePower);

    // fReflection *= 10.0f * Fresnel(mainLayout.f4EyePosition.xyz, f3InPosition, f3WaterNormal, globalLayout.f4WaterSix.x);
    // float fReflection = fReflectionTerrainMultiplier * 10.0f * Fresnel(mainLayout.f4EyePosition.xyz, f3InPosition, f3WaterNormal, globalLayout.f4WaterSix.x);
    float fSkyboxAdd = mainLayout.fLightingWaterSkyboxAdd;
    // f3LightingColor += fSkyboxAdd * fReflection * f3SkyboxColorSun + (1.0f - fSkyboxAdd) * fReflection * f3LightingColor * f3SkyboxColorSun;
    f3LightingColor = mix(f3LightingColor, f3SkyboxColorSun, fReflection);
    f3LightingColor += fSkyboxAdd * fReflection * f3SkyboxColorSun;
    // f3LightingColor += f3LightingColor * 5.0f * Fresnel(mainLayout.f4EyePosition.xyz, f3InPosition, vec3(0.0f, 0.0f, 1.0f), globalLayout.f4WaterSix.x);

    float fReflectionHeightMultiplier2 = clamp((f3InPosition.z - mainLayout.fWaterHeightDarkenBottom) / (mainLayout.fWaterHeightDarkenTop - mainLayout.fWaterHeightDarkenBottom), mainLayout.fWaterHeightDarkenClamp, 1.0f);
    f3LightingColor *= fReflectionHeightMultiplier2;
    f3LightingColor *= fSunlight;

    // Sunlight & shadow
	float fShadow = SmokeShadow(globalLayout, f3InPosition, smokeSampler, mainLayout.fSmokeShadowIntensity) * max(0.2f, texture(shadowTextureSampler, f2InVisibleAreaTexcoord).x) * texture(objectShadowsTextureSampler, f2InVisibleAreaTexcoord).x;
    f4OutColor.xyz = fShadow * f3LightingColor;
    f4OutColor.xyz = max(f4OutColor.xyz, 0.5f * globalLayout.f4AmbientColor.xyz * f3SkyboxColor);

	// Terrain elevation (for water transparency)
	f4OutColor.w = clamp(-fTerrainElevation / globalLayout.f4WaterOne.y, 0.0f, 1.0f);

	// Lighting and smoke at base height
    vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InPosition);
	vec2 f2BaseHeightTexcoord = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4VisibleArea);

    const float fSpecularNormalSoften = mainLayout.fLightingWaterSpecularNormalSoften;
    const float fSpecularNormalBlendWave = mainLayout.fLightingWaterSpecularNormalBlendWave;
	vec3 f3LightingNormal = (1.0f - fSpecularNormalBlendWave) * f3SampledNormal + fSpecularNormalBlendWave * f3InNormal;
    f3LightingNormal = vec3(0.0f, 0.0f, fSpecularNormalSoften) + f3LightingNormal;
    f3LightingNormal = normalize(f3LightingNormal); 

    vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2BaseHeightTexcoord);
    f4OutColor.xyz += fReflectionHeightMultiplier2 * fReflectionTerrainMultiplier * SpecularLighting(globalLayout, mainLayout, f3PreLightingColor, f3InPosition, f3InNormal, f3LightingNormal, pf4Lighting, mainLayout.fLightingWaterSpecularIntensity, mainLayout.fLightingWaterSpecularAdd);
    f4OutColor.xyz = AddSmoke(globalLayout, f4OutColor.xyz, f2PositionAtBaseHeight, smokeSampler, 1.0f, pf4Lighting);
}
