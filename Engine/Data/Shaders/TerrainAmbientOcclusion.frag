#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D textureSampler[kiMaxIslands];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out float fOut;

void main()
{
	fOut = globalLayout.f4Terrain.y * (1.0f - texture(textureSampler[iInInstanceIndex], f2InTexcoord).x);
}
