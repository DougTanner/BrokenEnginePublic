#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly hexShieldsUniform
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
	vec3 f3Normal = normalize(f3InNormal);
	for (int32_t j = 0; j < kiHexShieldDirections; ++j)
	{
		float fDot = dot(f3Normal, pHexShields[i].pf4Directions[j].xyz);
		float fFalloff = pow(max(0.5f + 0.5f * fDot, 0.0f), mainLayout.fHexShieldDirectionFalloffPower);
		fDirection += mainLayout.fHexShieldDirectionMultiplier * pHexShields[i].pfFragIntensities[j] * fFalloff;
	}

	// Lighting direction
	vec2 f2CenterXY = f3InCenterNormal.xy;
	float fCenterLen = length(f2CenterXY);
	vec2 f2Direction = fCenterLen > kfEpsilon ? f2CenterXY / fCenterLen : vec2(0.0f);
	vec4 f4Direction = vec4(f2Direction.x < 0.0f ? -f2Direction.x : 0.0f, f2Direction.x > 0.0f ? f2Direction.x : 0.0f, f2Direction.y < 0.0f ? -f2Direction.y : 0.0f, f2Direction.y > 0.0f ? f2Direction.y : 0.0f);

	// Compute all color channels simultaneously
	float fEdgeFade = LightingDepositEdgeFade(gl_FragCoord.xy, globalLayout.f2LightingDepositSizeInv);
	f4OutColorRed = fEdgeFade * pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.r * f4Direction;
	f4OutColorGreen = fEdgeFade * pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.g * f4Direction;
	f4OutColorBlue = fEdgeFade * pHexShields[i].fLightingIntensity * fDirection * pHexShields[i].f4LightingColor.b * f4Direction;
}
