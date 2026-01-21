#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) buffer readonly quadsUniform
{
	WidgetLayout pWidgets[kiMaxWidgets];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat uvec4 ui4OutMisc;
layout (location = 1) out vec2 f2OutTexcoord;
layout (location = 2) out vec4 f4OutColor;

void main()
{
	ui4OutMisc = pWidgets[gl_InstanceIndex].ui4Misc;

	f2OutTexcoord = vec2((1.0f - f2InQuadVertex.x) * pWidgets[gl_InstanceIndex].f4TextureRect.x + f2InQuadVertex.x * pWidgets[gl_InstanceIndex].f4TextureRect.z,
	                     (1.0f - f2InQuadVertex.y) * pWidgets[gl_InstanceIndex].f4TextureRect.y + f2InQuadVertex.y * pWidgets[gl_InstanceIndex].f4TextureRect.w);

	f4OutColor = unpackUnorm4x8(pWidgets[gl_InstanceIndex].ui4Misc.x).abgr;

	gl_Position = vec4(pWidgets[gl_InstanceIndex].f4VertexRect.x + f2InQuadVertex.x * pWidgets[gl_InstanceIndex].f4VertexRect.z,
	                   pWidgets[gl_InstanceIndex].f4VertexRect.y + f2InQuadVertex.y * pWidgets[gl_InstanceIndex].f4VertexRect.w,
					   0.0f,
					   1.0f);
}
