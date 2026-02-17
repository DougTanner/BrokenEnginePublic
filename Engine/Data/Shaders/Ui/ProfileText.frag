#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 1, binding = 2) uniform sampler2D textureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in flat uint uiInColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
    vec4 f4Color = unpackUnorm4x8(uiInColor);
    f4OutColor = vec4(f4Color.rgb, f4Color.a * texture(textureSampler, f2InTexcoord).r);
}
