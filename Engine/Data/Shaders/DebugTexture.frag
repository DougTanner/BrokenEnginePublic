#version 460

#include "ShaderLayouts.h"

// Sampler
layout (set = 1, binding = 1) uniform sampler2D debugSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	f4OutColor = vec4(texture(debugSampler, f2InTexcoord).rgb, 1.0f);
}
