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
	QuadLayout pQuads[];
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

	int iIndex = 2 * int(f2InQuadVertex.y) + int(f2InQuadVertex.x);

	f4OutMisc = pQuads[gl_InstanceIndex].pf4Misc[iIndex];

	f2OutTexcoord = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].zw;

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

	float fWorldX = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].x;
	float fWorldY = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].y;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - f4VisibleArea.x) / (f4VisibleArea.z - f4VisibleArea.x),
	                    1.0f - 2.0f * (fWorldY - f4VisibleArea.y) / (f4VisibleArea.w - f4VisibleArea.y),
					   0.0f,
					   1.0f);
}
