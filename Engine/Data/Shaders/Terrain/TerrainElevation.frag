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
layout (location = 7) in flat uint uiInTextureSlot;

// Output
layout (location = 0) out float fOutElevation;

void main()
{
    // Heightmap texel is engine-meters relative to beach (DataPacker pre-shifted per-island by
    // `Level × elevationMeters` read from the archetype Sea node). Beach = 0; negative = water;
    // positive = land. f4InMisc.x (was per-island beach threshold) is now unused — kept in the
    // vertex layout for future per-island params and forced to 0 from Islands.cpp so the
    // subtraction is a no-op.
    fOutElevation = texture(textureSampler[nonuniformEXT(uiInTextureSlot)], f2InTexcoord).r - f4InMisc.x;
}
