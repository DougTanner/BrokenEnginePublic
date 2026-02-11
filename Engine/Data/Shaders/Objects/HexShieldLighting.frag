#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, binding = 2) buffer readonly hexShieldsUniform
{
	HexShieldLayout pHexShields[];
};

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec3 f3InNormal;
layout (location = 3) in vec3 f3InOriginalPosition;
layout (location = 4) in vec3 f3InCenterNormal;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	int i = iInInstanceIndex;

	// Hex shield direction
	float fDirection = pHexShields[i].fMinimumIntensity;
	for (int32_t j = 0; j < kiHexShieldDirections; ++j)
	{
		float fDot = dot(f3InNormal, pHexShields[i].pf4Directions[j].xyz);
		float fFalloff = max(pow(0.5f + 0.5f * fDot, mainLayout.fHexShieldDirectionFalloffPower), 0.0f);
		fDirection += mainLayout.fHexShieldDirectionMultiplier * pHexShields[i].pfFragIntensities[j] * fFalloff;
	}

	// Lighting direction
	vec2 f2Direction = normalize(f3InCenterNormal.xy);
	vec4 f4Direction = vec4(f2Direction.x > 0.0f ? f2Direction.x : 0.0f, f2Direction.x < 0.0f ? -f2Direction.x : 0.0f, f2Direction.y > 0.0f ? f2Direction.y : 0.0f, f2Direction.y < 0.0f ? -f2Direction.y : 0.0f);

	// Compute all color channels simultaneously
	f4OutColorRed = pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.r * f4Direction;
	f4OutColorGreen = pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.g * f4Direction;
	f4OutColorBlue = pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.b * f4Direction;
}
