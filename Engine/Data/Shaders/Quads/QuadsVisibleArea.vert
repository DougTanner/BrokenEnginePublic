#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 1) buffer readonly quadsUniform
{
	QuadLayout pQuads[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec4 f4OutParams;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 4) out vec2 f2OutWorldPosition;
layout (location = 5) out flat vec2 f2OutWorldCenter;
// Interface match with QuadsAxisAlignedVisibleArea.vert so frags that pair with both verts (Smoke, WindDeposit) declare a single location 7 input.
layout (location = 7) out flat uint uiOutTextureSlot;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;
	uiOutTextureSlot = 0u;

	int iIndex = 2 * int(f2InQuadVertex.y) + int(f2InQuadVertex.x);

	f4OutParams = pQuads[gl_InstanceIndex].pf4Params[iIndex];

	f2OutTexcoord = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].zw;

	vec4 f4VisibleArea;
	if (int(pushConstantsLayout.f4Pipeline.x) == 0)
	{
		f4VisibleArea = globalLayout.f4VisibleArea;
	}
	else if (int(pushConstantsLayout.f4Pipeline.x) == 1)
	{
		f4VisibleArea = globalLayout.f4ShadowAreaExtra;
	}
	else if (int(pushConstantsLayout.f4Pipeline.x) == 2)
	{
		f4VisibleArea = globalLayout.f4SmokeArea;
	}
	else
	{
		f4VisibleArea = globalLayout.f4LightingArea;
	}

	float fWorldX = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].x;
	float fWorldY = pQuads[gl_InstanceIndex].pf4VerticesTexcoords[iIndex].y;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - f4VisibleArea.x) / (f4VisibleArea.z - f4VisibleArea.x),
	                    1.0f - 2.0f * (fWorldY - f4VisibleArea.y) / (f4VisibleArea.w - f4VisibleArea.y),
					   0.0f,
					   1.0f);

	f2OutWorldPosition = vec2(fWorldX, fWorldY);
	f2OutWorldCenter = (pQuads[gl_InstanceIndex].pf4VerticesTexcoords[0].xy
	                  + pQuads[gl_InstanceIndex].pf4VerticesTexcoords[1].xy
	                  + pQuads[gl_InstanceIndex].pf4VerticesTexcoords[2].xy
	                  + pQuads[gl_InstanceIndex].pf4VerticesTexcoords[3].xy) * 0.25f;
}
