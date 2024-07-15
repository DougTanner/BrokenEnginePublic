#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D textureSamplers[6];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 fOutColor;

void main()
{
    // This is here because in a very rare case the interpolater can send us a very small negative ex: -0.000000050728
    float fMiscY = max(0.0f, f4InMisc.y);

    vec2 f2Center = vec2(0.5f, 0.5f);
    float fTexture = texture(textureSamplers[int(f4InMisc.z + 0.4f)], f2Center + Rotate(f2InTexcoord - f2Center, f4InMisc.w)).x;

    fOutColor = fTexture * vec4(f4InMisc.x * pow(fMiscY, globalLayout.f4SmokeTwo.z) * globalLayout.f4SmokeOne.x, 0.0f, 0.0f, 1.0f);
}
