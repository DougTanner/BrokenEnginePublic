#version 460

#include "ShaderLayouts.h"

layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (location = 0) in vec2 f2InQuadVertex;

layout (location = 0) out flat int iOutInstanceIndex;
layout (location = 1) out vec2 f2OutTexcoord;

void main()
{
	iOutInstanceIndex = gl_InstanceIndex;

	int iPass = int(pushConstantsLayout.f4Pipeline.z);
	vec2 f2Extent = pushConstantsLayout.f4Pipeline.xy;
	vec2 f2Min = vec2(globalLayout.piLightingSpreadMinX[iPass], globalLayout.piLightingSpreadMinY[iPass]) / f2Extent;
	vec2 f2Max = vec2(globalLayout.piLightingSpreadMaxX[iPass], globalLayout.piLightingSpreadMaxY[iPass]) / f2Extent;
	vec2 f2WindowUv = mix(f2Min, f2Max, f2InQuadVertex);
	f2OutTexcoord = f2WindowUv;
	gl_Position = vec4(-1.0f + 2.0f * f2WindowUv.x, 1.0f - 2.0f * f2WindowUv.y, 0.0f, 1.0f);
}
