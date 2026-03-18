#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 1, binding = 2) uniform sampler2D textureSampler;
layout (set = 1, binding = 3) buffer smokeOccupancyBuffer { uint occupancy[]; };

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 fOutColor;

void main()
{
    // This is here because in a very rare case the interpolater can send us a very small negative ex: -0.000000050728
    float fMiscY = max(0.0f, f4InParams.y);

    vec2 f2Center = vec2(0.5f, 0.5f);
    float fTexture = texture(textureSampler, f2Center + Rotate(f2InTexcoord - f2Center, f4InParams.w)).x;

    fOutColor = fTexture * vec4(f4InParams.x * pow(fMiscY, globalLayout.fSmokeIntensityFalloff) * 0.0166666657f, 0.0f, 0.0f, 1.0f);

    // Mark occupancy for deposited tiles (seeds hierarchical dispatch)
    if (fOutColor.x > 0.0f)
    {
        ivec2 i2TileCoord = ivec2(gl_FragCoord.xy * globalLayout.fSmokeDepositTileScale) / 8;
        uint uiTileIndex = i2TileCoord.y * globalLayout.uiSmokeTilesX + i2TileCoord.x;
        atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
    }
}
