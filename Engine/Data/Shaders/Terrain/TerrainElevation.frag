#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 1, binding = 2) uniform sampler2D textureSampler[kiMaxIslands];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out float fOutElevation;

void main()
{
    float fElevation = texture(textureSampler[nonuniformEXT(iInInstanceIndex)], f2InTexcoord).r - f4InMisc.x;
    if (fElevation >= 0.0f)
    {
        fOutElevation = globalLayout.fIslandHeight * fElevation;
    }
    else
    {
        fOutElevation = globalLayout.fWaterDepth * fElevation;
    }
}
