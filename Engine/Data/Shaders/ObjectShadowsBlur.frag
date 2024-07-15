#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform sampler2D objectShadowsTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out float fOutColor;

void main()
{
	const float fDistanceMultiplier = globalLayout.f4ShadowThree.x;

	const int iKernelSize = 9;
	float fTotal = 0.0f;
	for (int j = 0; j < iKernelSize; ++j)
	{
		for (int i = 0; i < iKernelSize; ++i)
		{
			vec2 f2Offset = fDistanceMultiplier * vec2(float(-2 + i), float(-2 + j));
			fTotal += (1.0f - texture(objectShadowsTextureSampler, f2InTexcoord + f2Offset).x);
		}
	}

	fOutColor = 1.0f - globalLayout.f4ShadowThree.z * fTotal / float(iKernelSize * iKernelSize);
}
