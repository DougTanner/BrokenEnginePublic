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
layout (location = 0) in vec3 f3InPosition;
layout (location = 1) in vec3 f3InNormal;

// Output
layout (location = 0) out flat int32_t iOutInstanceIndex;
layout (location = 1) out vec3 f3OutPosition;
layout (location = 2) out vec3 f3OutNormal;
layout (location = 3) out vec3 f3OutOriginalPosition;
layout (location = 4) out vec3 f3OutCenterNormal;

void main()
{
	int32_t i = int32_t(gl_InstanceIndex);
	iOutInstanceIndex = i;

	f3OutPosition = f3InPosition;
	f3OutOriginalPosition = f3InPosition;
	f3OutNormal = f3InNormal;

	// Rotation
	f3OutPosition = Transform(vec4(f3OutPosition, 1.0f), pHexShields[i].f3x4Transform);
	f3OutNormal = normalize(Transform(vec4(f3OutNormal, 0.0f), pHexShields[i].f3x4TransformNormal));
	f3OutCenterNormal = normalize(normalize(f3OutPosition) + f3OutNormal);

	// Grow
	f3OutPosition += f3OutNormal * mainLayout.fHexShieldGrow;

	// Direction wave
	float fDirection = 0.0f;
	for (int32_t j = 0; j < kiHexShieldDirections; ++j)
	{
		if (pHexShields[i].pfVertIntensities[j] <= 0.0f)
		{
			continue;
		}

		float fDot = dot(f3OutNormal, pHexShields[i].pf4Directions[j].xyz);
		float fWave = sin(mainLayout.fHexShieldWaveDotMultiplier * fDot + mainLayout.fHexShieldWaveIntensityMultiplier * pow(pHexShields[i].pfVertIntensities[j], mainLayout.fHexShieldWaveIntensityPower));
		float fFalloff = 0.5f * max(pow(0.5f + 0.5f * fDot, mainLayout.fHexShieldWaveFalloffPower), 0.0f);
		f3OutPosition += f3OutNormal * mainLayout.fHexShieldWaveMultiplier * pHexShields[i].pfVertIntensities[j] * fWave * fFalloff;
	}

	// Size
	f3OutPosition *= pHexShields[i].fSize;

	// World position
	f3OutPosition += pHexShields[i].f4Position.xyz;

	if (int(pushConstantsLayout.f4Pipeline.y) == 0)
	{
		gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else
	{
		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3OutPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(vec2(-1.0f + 2.0f * f2VisibleAreaPosition.x, 1.0f - 2.0f * f2VisibleAreaPosition.y), 0.0f, 1.0f);
	}
}
