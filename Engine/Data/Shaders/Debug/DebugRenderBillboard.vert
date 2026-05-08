#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 1) uniform mainUniform
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

	vec4 row0 = pDebugRenders[i].f3x4Transform[0];
	vec4 row1 = pDebugRenders[i].f3x4Transform[1];
	vec4 row2 = pDebugRenders[i].f3x4Transform[2];

	vec3 center = vec3(row0.w, row1.w, row2.w);
	float scale = length(vec3(row0.x, row1.x, row2.x));

	vec3 forward = mainLayout.f4ToEyeNormal.xyz;
	vec3 worldUp = abs(forward.z) < 0.999f ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
	vec3 right = normalize(cross(worldUp, forward));
	vec3 up = cross(forward, right);

	vec4 f3x4Billboard[3];
	f3x4Billboard[0] = vec4(right * scale, center.x);
	f3x4Billboard[1] = vec4(up * scale, center.y);
	f3x4Billboard[2] = vec4(forward * scale, center.z);

	vec3 f3WorldPosition = Transform(vec4(f3InPosition, 1.0f), f3x4Billboard);
	gl_Position = Transform(vec4(f3WorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
