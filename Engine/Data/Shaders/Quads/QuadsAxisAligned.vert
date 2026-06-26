#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 1) buffer readonly quadsUniform
{
	AxisAlignedQuadLayout pQuads[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out vec2 f2OutTexcoord;
layout (location = 1) out flat uint uiOutColor;

void main()
{
	uiOutColor = pQuads[gl_InstanceIndex].uiColor;

	f2OutTexcoord = vec2((1.0f - f2InQuadVertex.x) * pQuads[gl_InstanceIndex].f4TextureRect.x + f2InQuadVertex.x * pQuads[gl_InstanceIndex].f4TextureRect.z,
	                     (1.0f - f2InQuadVertex.y) * pQuads[gl_InstanceIndex].f4TextureRect.y + f2InQuadVertex.y * pQuads[gl_InstanceIndex].f4TextureRect.w);

	gl_Position = vec4(pQuads[gl_InstanceIndex].f4VertexRect.x + f2InQuadVertex.x * pQuads[gl_InstanceIndex].f4VertexRect.z,
	                   pQuads[gl_InstanceIndex].f4VertexRect.y + f2InQuadVertex.y * pQuads[gl_InstanceIndex].f4VertexRect.w,
					   0.0f,
					   1.0f);
}
