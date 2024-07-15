#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

layout (binding = 2) uniform sampler2D textureSampler[kiMaxIslands];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec3 f3OutColor;

void main()
{
    f3OutColor = texture(textureSampler[iInInstanceIndex], f2InTexcoord).rgb;
}
