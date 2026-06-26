#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (set = 1, binding = 2) uniform sampler2D textureSampler;
layout (set = 1, binding = 3) buffer smokeOccupancyBuffer { uint occupancy[]; };

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;
// Match QuadsAxisAlignedVisibleArea.vert output so SPIR-V interface validation passes; not read here.
layout (location = 7) in flat uint uiInTextureSlotUnused;

// Output
layout (location = 0) out vec4 fOutColor;

// Frame-rate-independent deposit normalization (smoke intensity is tuned against 60 Hz simulation cadence).
const float kfSmokeDepositNormPerFrame = 1.0f / 60.0f;

void main()
{
    // This is here because in a very rare case the interpolater can send us a very small negative ex: -0.000000050728
    float fMiscY = max(0.0f, f4InParams.y);

    vec2 f2Center = vec2(0.5f, 0.5f);
    float fTexture = texture(textureSampler, f2Center + Rotate(f2InTexcoord - f2Center, f4InParams.w)).x;

    // gSmokeIntensityFalloff slider min 0.1 keeps pow() base >= 0 -> always defined.
    fOutColor = fTexture * vec4(f4InParams.x * pow(fMiscY, globalLayout.fSmokeIntensityFalloff) * kfSmokeDepositNormPerFrame, 0.0f, 0.0f, 1.0f);

    // Mark occupancy for deposited tiles (seeds hierarchical dispatch)
    if (fOutColor.x > 0.0f)
    {
        ivec2 i2TileCoord = ivec2(gl_FragCoord.xy * globalLayout.fSmokeDepositTileScale) / kiComputeTileSize;
        uint uiTileIndex = i2TileCoord.y * globalLayout.uiSmokeTilesX + i2TileCoord.x;
        atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
    }
}
