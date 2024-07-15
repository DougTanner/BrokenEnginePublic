#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;

	f2OutTexcoord = f2InQuadVertex;

	gl_Position = vec4(-1.0f + 2.0f * f2InQuadVertex.x, 1.0f - 2.0f * f2InQuadVertex.y, 0.0f, 1.0f);
}
