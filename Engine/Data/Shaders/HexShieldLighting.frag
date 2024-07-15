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

layout (binding = 2) buffer readonly hexShieldsUniform
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
layout (location = 0) out vec4 f4OutColor;

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

	float fColor = 0.0f;
	float fColorEdge = 0.0f;
	if (int(pushConstantsLayout.f4Pipeline.z) == 0)
	{
		fColor = pHexShields[i].f4LightingColor.r;
		fColorEdge = 1.0f;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 1)
	{
		fColor = pHexShields[i].f4LightingColor.g;
		fColorEdge = 0.75f;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 2)
	{
		fColor = pHexShields[i].f4LightingColor.b;
		fColorEdge = 0.1f;
	}

	// Lighting direction
	vec2 f2Direction = normalize(f3InCenterNormal.xy);
	vec4 f4Direction = vec4(f2Direction.x > 0.0f ? f2Direction.x : 0.0f, f2Direction.x < 0.0f ? -f2Direction.x : 0.0f, f2Direction.y > 0.0f ? f2Direction.y : 0.0f, f2Direction.y < 0.0f ? -f2Direction.y : 0.0f);

    f4OutColor = pHexShields[i].fLightingIntensity * fDirection * fColor * f4Direction;
}
