#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly debugRenderUniform
{
	DebugRenderLayout pDebugRenders[];
};

// Input
layout (location = 0) in vec3 f3InPosition;

// Output
layout (location = 0) out flat vec4 f4OutColor;

void main()
{
	int32_t i = int32_t(gl_InstanceIndex);
	f4OutColor = pDebugRenders[i].f4Color;
	vec3 f3WorldPosition = Transform(vec4(f3InPosition, 1.0f), pDebugRenders[i].f3x4Transform);
	gl_Position = Transform(vec4(f3WorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
