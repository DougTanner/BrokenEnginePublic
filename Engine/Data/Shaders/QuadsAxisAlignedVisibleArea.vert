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

layout (binding = 1) buffer readonly quadsUniform
{
	AxisAlignedQuadLayout pQuads[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec4 f4OutMisc;
layout (location = 2) out vec2 f2OutTexcoord;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;
	f4OutMisc = pQuads[gl_InstanceIndex].f4Misc;

	f2OutTexcoord = vec2((1.0f - f2InQuadVertex.x) * pQuads[gl_InstanceIndex].f4TextureRect.x + f2InQuadVertex.x * pQuads[gl_InstanceIndex].f4TextureRect.z,
	                     (1.0f - f2InQuadVertex.y) * pQuads[gl_InstanceIndex].f4TextureRect.y + f2InQuadVertex.y * pQuads[gl_InstanceIndex].f4TextureRect.w);

	vec4 f4VisibleArea;
	if (int(pushConstantsLayout.f4Pipeline.x) == 0)
	{
		f4VisibleArea = globalLayout.f4VisibleArea;
	}
	else if (int(pushConstantsLayout.f4Pipeline.x) == 1)
	{
		f4VisibleArea = globalLayout.f4VisibleAreaShadowsExtra;
	}
	else if (int(pushConstantsLayout.f4Pipeline.x) == 2)
	{
		f4VisibleArea = globalLayout.f4SmokeArea;
	}

	float fWorldX = pQuads[gl_InstanceIndex].f4VertexRect.x + f2InQuadVertex.x * pQuads[gl_InstanceIndex].f4VertexRect.z;
	float fWorldY = pQuads[gl_InstanceIndex].f4VertexRect.y + f2InQuadVertex.y * pQuads[gl_InstanceIndex].f4VertexRect.w;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - f4VisibleArea.x) / (f4VisibleArea.z - f4VisibleArea.x),
	                    1.0f - 2.0f * (fWorldY - f4VisibleArea.y) / (f4VisibleArea.w - f4VisibleArea.y),
					   0.0f,
					   1.0f);
}
