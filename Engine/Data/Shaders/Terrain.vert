#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 1) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (binding = 5) uniform sampler2D elevationTextureSampler;

// Input
layout (location = 0) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec2 f2OutTexcoord;

void main()
{
	vec3 f3OutWorldPosition = vec3
	(
		(1.0f - f2InTexcoord.x) * globalLayout.f4VisibleArea.x + f2InTexcoord.x * globalLayout.f4VisibleArea.z,
		(1.0f - f2InTexcoord.y) * globalLayout.f4VisibleArea.y + f2InTexcoord.y * globalLayout.f4VisibleArea.w,
		texture(elevationTextureSampler, f2InTexcoord).r
	);

	f2OutTexcoord = f2InTexcoord;

	gl_Position = Transform(vec4(f3OutWorldPosition, 1.0f), mainLayout.f4x4ViewProjection);
}
