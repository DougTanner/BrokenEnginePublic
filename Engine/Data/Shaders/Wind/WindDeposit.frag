#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D textureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fFalloff = texture(textureSampler, f2InTexcoord).r;
	vec2 f2WindDir = f4InParams.yz;
	float fMagnitude = f4InParams.x;
	f4OutColor = vec4(fFalloff * fMagnitude * f2WindDir, 0.0, 1.0);
}
