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
layout (location = 0) in vec3 f3InPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InUV;
layout (location = 3) in vec2 f2InUV1;
layout (location = 4) in vec2 f2InUV2;
layout (location = 5) in vec2 f2InUV3;
layout (location = 6) in vec2 f2InUV4;
layout (location = 7) in float fJoint;
layout (location = 8) in vec4 f4Joint0;
layout (location = 9) in vec4 f4Weight0;

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
	for (int32_t j = 0; j < kiHexShieldDirections; ++j)
	{
		if (pHexShields[i].pfVertIntensities[j] <= 0.0f)
		{
			continue;
		}

		float fDot = dot(f3OutNormal, pHexShields[i].pf4Directions[j].xyz);
		float fWave = sin(mainLayout.fHexShieldWaveDotMultiplier * fDot + mainLayout.fHexShieldWaveIntensityMultiplier * pow(pHexShields[i].pfVertIntensities[j], mainLayout.fHexShieldWaveIntensityPower));
		float fFalloff = 0.5f * pow(max(0.5f + 0.5f * fDot, 0.0f), mainLayout.fHexShieldWaveFalloffPower);
		f3OutPosition += f3OutNormal * mainLayout.fHexShieldWaveMultiplier * pHexShields[i].pfVertIntensities[j] * fWave * fFalloff;
	}

	// Size
	f3OutPosition *= pHexShields[i].fSize;

	// World position
	f3OutPosition += pHexShields[i].f4Position.xyz;

	if (int(pushConstantsLayout.f4Pipeline.x) == 0)
	{
		gl_Position = Transform(vec4(f3OutPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else
	{
		// Eye-line / base-height intersection (mirrors CPU ToBaseHeight; unclamped, unlike BaseHeightPosition which is no-op for z > base)
		vec3 f3ToEye = mainLayout.f4EyePosition.xyz - f3OutPosition;
		float fT = (globalLayout.fBaseHeight - f3OutPosition.z) / f3ToEye.z;
		vec3 f3BasePosition = f3OutPosition + fT * f3ToEye;
		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3BasePosition, globalLayout.f4LightingArea);
		gl_Position = vec4(vec2(-1.0f + 2.0f * f2VisibleAreaPosition.x, 1.0f - 2.0f * f2VisibleAreaPosition.y), 0.0f, 1.0f);
	}
}
