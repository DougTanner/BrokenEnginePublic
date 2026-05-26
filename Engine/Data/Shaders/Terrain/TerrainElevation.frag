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
    // subtraction is a no-op. Underwater samples pass through a power curve anchored at sea floor:
    // beach (t=0) and floor (t=1) are fixed points; shallow water is pulled up toward beach with
    // strength controlled by globalLayout.fWaterUnderseaCompression (1.0 = identity, smaller =
    // higher exponent = stronger upward pull on the upper undersea band). Every downstream
    // consumer of mTerrainElevationTexture inherits the curved depth from this single upstream
    // point.
    float fRaw = texture(textureSampler[nonuniformEXT(uiInTextureSlot)], f2InTexcoord).r - f4InMisc.x;
    float fT = clamp(fRaw / globalLayout.fSeaFloorElevation, 0.0, 1.0);
    float fCurved = pow(fT, 1.0 / globalLayout.fWaterUnderseaCompression) * globalLayout.fSeaFloorElevation;
    fOutElevation = mix(fCurved, fRaw, step(0.0, fRaw));
}
