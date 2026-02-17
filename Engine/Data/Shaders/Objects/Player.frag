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

layout (set = 1, binding = 3) uniform sampler2D pLightingSamplers[3];
layout (set = 1, binding = 4) uniform sampler2D shadowTextureSampler;
layout (set = 1, binding = 5) uniform samplerCube skyboxSampler;

// Input
layout (location = 0) in vec3 f3InWorldPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in flat uint uiInColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(f3InWorldPosition, globalLayout.f4VisibleArea);

    float fShadow = texture(shadowTextureSampler, f2VisibleAreaTexcoord).x;
    vec3 f3Color = SunLighting(vec3(1.0f, 1.0f, 1.0f), globalLayout, vec4(f3InWorldPosition, 1.0f), f3InNormal, fShadow, 1.0f);

    // Reflected normal
    vec3 f3IncidentNormal = normalize(f3InWorldPosition - mainLayout.f4EyePosition.xyz);
    vec3 f3ReflectedNormal = normalize(reflect(f3IncidentNormal, f3InNormal));

    // Skybox cubemap
    float fSkyboxShadow = 0.5f * (1.0f - fShadow) + fShadow;
    vec3 f3SkyboxColor = fSkyboxShadow * globalLayout.f4SunColor.xyz * texture(skyboxSampler, f3ReflectedNormal.xzy).xyz;

    // Final color
    f4OutColor = vec4(f3SkyboxColor, 1.0f);

    // Lighting
    vec4 pf4Lighting[3];
	pf4Lighting[0] = texture(pLightingSamplers[0], f2VisibleAreaTexcoord);
	pf4Lighting[1] = texture(pLightingSamplers[1], f2VisibleAreaTexcoord);
	pf4Lighting[2] = texture(pLightingSamplers[2], f2VisibleAreaTexcoord);
	f4OutColor.xyz += Lighting(globalLayout, vec3(1.0f, 1.0f, 1.0f), f3InWorldPosition.z, f3InNormal, pf4Lighting, globalLayout.fLightingObjects, globalLayout.fLightingObjectsAdd);
}
