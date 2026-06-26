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
	AxisAlignedQuadLayout pQuads[];
};

// Input
layout (location = 0) in vec2 f2InQuadVertex;

// Output
layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec4 f4OutParams;
layout (location = 2) out vec2 f2OutTexcoord;
layout (location = 4) out vec2 f2OutWorldPosition;
layout (location = 5) out flat vec2 f2OutWorldCenter;
layout (location = 7) out flat uint uiOutTextureSlot;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;
	f4OutParams = pQuads[gl_InstanceIndex].f4Params;
	uiOutTextureSlot = pQuads[gl_InstanceIndex].uiTextureSlot;

	f2OutTexcoord = vec2((1.0f - f2InQuadVertex.x) * pQuads[gl_InstanceIndex].f4TextureRect.x + f2InQuadVertex.x * pQuads[gl_InstanceIndex].f4TextureRect.z,
	                     (1.0f - f2InQuadVertex.y) * pQuads[gl_InstanceIndex].f4TextureRect.y + f2InQuadVertex.y * pQuads[gl_InstanceIndex].f4TextureRect.w);

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

	// fRotation is per-quad angle in radians; 0 (default) = identity (no rotation).
	float fCos = cos(pQuads[gl_InstanceIndex].fRotation);
	float fSin = sin(pQuads[gl_InstanceIndex].fRotation);
	float fLocalX = (f2InQuadVertex.x - 0.5f) * pQuads[gl_InstanceIndex].f4VertexRect.z;
	float fLocalY = (f2InQuadVertex.y - 0.5f) * pQuads[gl_InstanceIndex].f4VertexRect.w;
	float fRotX = fLocalX * fCos - fLocalY * fSin;
	float fRotY = fLocalX * fSin + fLocalY * fCos;
	float fCenterX = pQuads[gl_InstanceIndex].f4VertexRect.x + pQuads[gl_InstanceIndex].f4VertexRect.z * 0.5f;
	float fCenterY = pQuads[gl_InstanceIndex].f4VertexRect.y + pQuads[gl_InstanceIndex].f4VertexRect.w * 0.5f;
	float fWorldX = fCenterX + fRotX;
	float fWorldY = fCenterY + fRotY;
	gl_Position = vec4(-1.0f + 2.0f * (fWorldX - f4VisibleArea.x) / (f4VisibleArea.z - f4VisibleArea.x),
	                    1.0f - 2.0f * (fWorldY - f4VisibleArea.y) / (f4VisibleArea.w - f4VisibleArea.y),
	                    0.0f,
	                    1.0f);

	f2OutWorldPosition = vec2(fWorldX, fWorldY);
	f2OutWorldCenter = vec2(fCenterX, fCenterY);
}
