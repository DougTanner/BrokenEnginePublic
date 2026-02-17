	#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly objectsUniform
{
	ObjectLayout pObjects[];
};

// Input
layout (location = 0) in vec3 f3InPosition;
layout (location = 1) in vec3 f3InNormal;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec3 f3OutWorldPosition;
layout (location = 1) out vec3 f3OutNormal;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 3) out flat uint uiOutColor;

void main()
{
	uiOutColor = pObjects[gl_InstanceIndex].uiColor;

	f3OutWorldPosition = Transform(vec4(f3InPosition, 1.0f), pObjects[gl_InstanceIndex].f3x4Transform);

	if (int(pushConstantsLayout.f4Pipeline.x) == 0)
	{
		f3OutNormal = normalize(Transform(vec4(f3InNormal, 0.0f), pObjects[gl_InstanceIndex].f3x4TransformNormal));
		f2OutTexcoord = f2InTexcoord;

		gl_Position = Transform(vec4(f3OutWorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else
	{
		float fSunriseOffset = globalLayout.fShadowSunriseStretch;
		float fSunsetOffset = globalLayout.fShadowSunsetStretch;

		float fSunriseOffsetCubed = fSunriseOffset * fSunriseOffset * fSunriseOffset;
		float fSunsetOffsetCubed = fSunsetOffset * fSunsetOffset * fSunsetOffset;
		vec2 f2Translation = (fSunriseOffsetCubed + fSunsetOffsetCubed) * -globalLayout.f4SunNormal.xy;

		float fSunriseDiff = max(0.0f, pObjects[gl_InstanceIndex].f4Position.x - f3OutWorldPosition.x);
		float fSunsetDiff = max(0.0f, f3OutWorldPosition.x - pObjects[gl_InstanceIndex].f4Position.x);
		float fStretchX = -(0.5f + fSunriseDiff) * fSunriseOffsetCubed + (0.5f + fSunsetDiff) * fSunsetOffsetCubed;

		vec3 f3ShadowPosition = f3OutWorldPosition + vec3(f2Translation.x + fStretchX, f2Translation.y, 0.0f);

		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3ShadowPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(2.0f * f2VisibleAreaPosition.x - 1.0f, 1.0f - 2.0f * f2VisibleAreaPosition.y, 0.0f, 1.0f);
	}
}
