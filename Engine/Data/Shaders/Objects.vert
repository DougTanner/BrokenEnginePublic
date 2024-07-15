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

layout (binding = 2) buffer readonly objectsUniform
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
layout (location = 3) out flat uvec4 ui4OutMisc;

void main()
{
	ui4OutMisc = pObjects[gl_InstanceIndex].ui4Misc;

	f3OutWorldPosition = Transform(vec4(f3InPosition, 1.0f), pObjects[gl_InstanceIndex].f3x4Transform);

	if (int(pushConstantsLayout.f4Pipeline.x) == 0)
	{
		f3OutNormal = normalize(Transform(vec4(f3InNormal, 0.0f), pObjects[gl_InstanceIndex].f3x4TransformNormal));
		f2OutTexcoord = f2InTexcoord;

		gl_Position = Transform(vec4(f3OutWorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
	}
	else
	{
		float fSunriseDiff = max(0.0f, pObjects[gl_InstanceIndex].f4Position.x - f3OutWorldPosition.x);
		f3OutWorldPosition.x -= (0.5f + fSunriseDiff) * globalLayout.f4ShadowFour.x * globalLayout.f4ShadowFour.x * globalLayout.f4ShadowFour.x;

		float fSunsetDiff = max(0.0f, f3OutWorldPosition.x - pObjects[gl_InstanceIndex].f4Position.x);
		f3OutWorldPosition.x += (0.5f + fSunsetDiff) * globalLayout.f4ShadowFour.y * globalLayout.f4ShadowFour.y * globalLayout.f4ShadowFour.y;
		
		vec2 f2VisibleAreaPosition = WorldToVisibleArea(f3OutWorldPosition, globalLayout.f4VisibleArea);
		gl_Position = vec4(vec2(-1.0f + 2.0f * f2VisibleAreaPosition.x, 1.0f - 2.0f * f2VisibleAreaPosition.y), 0.0f, 1.0f);
	}
}
