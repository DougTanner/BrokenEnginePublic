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

layout (scalar, set = 1, binding = 2) buffer readonly hexShieldsUniform
{
	HexShieldLayout pHexShields[];
};

layout (set = 1, binding = 3) uniform samplerCube skyboxSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec3 f3InNormal;
layout (location = 3) in vec3 f3InOriginalPosition;
layout (location = 4) in vec3 f3InCenterNormal;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	int i = iInInstanceIndex;

	// Color
    vec3 f3IncidentNormal = normalize(f3InPosition - mainLayout.f4EyePosition.xyz);
    vec3 f3ReflectedNormal = reflect(f3IncidentNormal, normalize(f3InCenterNormal));
    vec3 f3SkyboxColor = texture(skyboxSampler, f3ReflectedNormal).xyz;

	f4OutColor.xyz = mix(f3SkyboxColor, pHexShields[i].f4Color.xyz, pHexShields[i].fColorMix);

	// Direction
	f4OutColor.a = pHexShields[i].fMinimumIntensity;
	vec3 f3Normal = normalize(f3InNormal);
	for (int32_t j = 0; j < kiHexShieldDirections; ++j)
	{
		float fDot = dot(f3Normal, pHexShields[i].pf4Directions[j].xyz);
		float fFalloff = max(pow(0.6f + 0.4f * fDot, mainLayout.fHexShieldDirectionFalloffPower), 0.0f);
		f4OutColor.a += mainLayout.fHexShieldDirectionMultiplier * pHexShields[i].pfFragIntensities[j] * fFalloff;
	}

	// Edge
	float fEdgeMultiplier = mainLayout.fHexShieldEdgeMultiplier * pow(max(length(f3InOriginalPosition) - mainLayout.fHexShieldEdgeDistance, 0.0f), mainLayout.fHexShieldEdgePower);
	f4OutColor.a *= pHexShields[i].f4Color.a * fEdgeMultiplier;
}
