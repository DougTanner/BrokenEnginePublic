#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniform buffers
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
layout (binding = 6) uniform sampler2D colorTextureSampler;
layout (binding = 7) uniform sampler2D normalTextureSampler;
layout (binding = 8) uniform sampler2D ambientOcclusionTextureSampler;
layout (binding = 9) uniform sampler2D smokeSampler;
layout (binding = 10) uniform sampler2D rockSampler;
layout (binding = 11) uniform sampler2D sandNormalsSampler0;
layout (binding = 12) uniform sampler2D sandNormalsSampler1;
layout (binding = 13) uniform sampler2D sandNormalsSampler2;
layout (binding = 14) uniform sampler2D sandSampler;
layout (binding = 15) uniform sampler2D rockNormalsSampler0;
layout (binding = 16) uniform sampler2D rockNormalsSampler1;
layout (binding = 17) uniform sampler2D rockNormalsSampler2;

// Input
layout (location = 0) in vec2 f2InVisibleAreaTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec3 f3InPosition = vec3
	(
		(1.0f - f2InVisibleAreaTexcoord.x) * globalLayout.f4VisibleArea.x + f2InVisibleAreaTexcoord.x * globalLayout.f4VisibleArea.z,
		(1.0f - f2InVisibleAreaTexcoord.y) * globalLayout.f4VisibleArea.y + f2InVisibleAreaTexcoord.y * globalLayout.f4VisibleArea.w,
		texture(elevationTextureSampler, f2InVisibleAreaTexcoord).x
	);

	if (f3InPosition.z < globalLayout.f4Terrain.z)
	{
		f4OutColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}
	
	vec3 f3Color = texture(colorTextureSampler, f2InVisibleAreaTexcoord).xyz;

	vec3 f3Normal = texture(normalTextureSampler, f2InVisibleAreaTexcoord).xyz;
	f3Normal.x = 1.0f - 2.0f * f3Normal.x;
	f3Normal.x *= globalLayout.fTerrainNormalXMultiplier;
	f3Normal.y = 1.0f - 2.0f * f3Normal.y;
	f3Normal.y *= globalLayout.fTerrainNormalYMultiplier;
	f3Normal = normalize(f3Normal);

	vec3 f3SnowDiff = abs(f3Color - vec3(1.0f, 1.0f, 1.0f));
	float fSnowPercent = 1.0f - clamp(globalLayout.fTerrainSnowMultiplier * (f3SnowDiff.x + f3SnowDiff.y + f3SnowDiff.z), 0.0f, 1.0f);

	float fRockPercent = clamp(f3InPosition.z / globalLayout.fTerrainBeachHeight, 0.0f, 1.0f);
	vec3 f3RockDiff = abs(f3Color - vec3(210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f));
	fRockPercent *= 1.0f - clamp(globalLayout.fTerrainRockMultiplier * (f3RockDiff.x + f3RockDiff.y + f3RockDiff.z), 0.0f, 1.0f);

	float fBeachPercent = 1.0f - clamp(f3InPosition.z / globalLayout.fTerrainBeachHeight, 0.0f, 1.0f);
	fBeachPercent *= 1.0f - clamp(2.0f * f3Color.g - f3Color.r - f3Color.b, 0.0f, 1.0f);

	if (fRockPercent > 0.001f)
	{
		vec3 f3RockNormal = normalize(SampleNormal(globalLayout, rockNormalsSampler0, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeOne, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)) +
									  SampleNormal(globalLayout, rockNormalsSampler1, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeTwo, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)) +
									  SampleNormal(globalLayout, rockNormalsSampler2, f3InPosition.xy + f3InPosition.z, globalLayout.fTerrainRockNormalsSizeThree, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)));
		f3Normal = normalize(f3Normal + globalLayout.fTerrainRockNormalsBlend * fRockPercent * dot(f3RockNormal, f3Normal));

		f3Color = mix(f3Color, texture(rockSampler, globalLayout.fTerrainRockSize * f3InPosition.xy).xyz, globalLayout.fTerrainRockBlend * fRockPercent);
	}

	if (fBeachPercent > 0.001f)
	{
		vec3 f3BeachNormal = normalize(2.0f * SampleNormal(globalLayout, sandNormalsSampler0, f3InPosition.xy, globalLayout.fTerrainBeachNormalsSizeOne, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)) +
									   0.5f * SampleNormal(globalLayout, sandNormalsSampler1, f3InPosition.yx, globalLayout.fTerrainBeachNormalsSizeTwo, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)) +
									   1.0f * SampleNormal(globalLayout, sandNormalsSampler2, f3InPosition.yx, globalLayout.fTerrainBeachNormalsSizeThree, 0.0f, globalLayout.f4Misc.x, vec2(0.0f, 0.0f)));
		f3BeachNormal.z = 0.0f;
		f3Normal = normalize(f3Normal + globalLayout.fTerrainBeachNormalsBlend * fBeachPercent * f3BeachNormal);

		f3Color = mix(f3Color, texture(sandSampler, globalLayout.fTerrainBeachSandSize * f3InPosition.xy).xyz, globalLayout.fTerrainBeachSandBlend * fBeachPercent);
	}

	vec3 f3SunNormal = normalize(f3Normal + fSnowPercent * globalLayout.f4SunNormal.xyz);
	float fShadow = SmokeShadow(globalLayout, f3InPosition, smokeSampler, mainLayout.fSmokeShadowIntensity) * max(0.2f, texture(shadowTextureSampler, f2InVisibleAreaTexcoord).x) * texture(objectShadowsTextureSampler, f2InVisibleAreaTexcoord).x;
    f4OutColor = vec4(SunLighting(f3Color, globalLayout, vec4(f3InPosition, 1.0f), f3SunNormal, fShadow, 1.0f - texture(ambientOcclusionTextureSampler, f2InVisibleAreaTexcoord).x), 1.0f);

	// Lighting and smoke at base height
    vec2 f2PositionAtBaseHeight = BaseHeightPosition(globalLayout, mainLayout, f3InPosition);
	vec2 f2BaseHeightTexcoord = WorldToVisibleArea(vec3(f2PositionAtBaseHeight, 0.0f), globalLayout.f4VisibleArea);

    vec4 pf4Lighting[3];
	ReadLighting(pf4Lighting, pLightingSamplers, f2BaseHeightTexcoord);
    f4OutColor.xyz += Lighting(globalLayout, f3Color, f3InPosition.z, f3Normal, pf4Lighting, globalLayout.f4LightingTwo.z, globalLayout.f4LightingThree.y);
    f4OutColor.xyz = AddSmoke(globalLayout, f4OutColor.xyz, f2PositionAtBaseHeight, smokeSampler, 1.0f, pf4Lighting);
}
