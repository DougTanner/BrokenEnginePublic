#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 0, binding = kiGlobalBindingMainUniform) uniform mainUniform
{
	MainLayout mainLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly visibleLightsUniform
{
	VisibleLightQuadLayout pQuads[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec3 f3OutPosition;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 3) out vec4 f4OutColor;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;

	int iIndex = 2 * int(f2InQuadVertex.y) + int(f2InQuadVertex.x);

	f3OutPosition = pQuads[gl_InstanceIndex].pf4Vertices[iIndex].xyz;
	f2OutTexcoord = pQuads[gl_InstanceIndex].pf4Texcoords[iIndex].xy;
	f4OutColor = unpackUnorm4x8(pQuads[gl_InstanceIndex].puiColors[iIndex]).abgr;

	vec4 f4Position = pQuads[gl_InstanceIndex].pf4Vertices[iIndex];
	f4Position.w = 1.0f;
	gl_Position = Transform(f4Position, mainLayout.f4x4ViewProjection);
}
