#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

layout (binding = 1) uniform sampler texturesSampler;
layout (binding = 2) uniform texture2D pTextures[kiMaxTextureCount];

// Input
layout (location = 0) in flat uvec4 ui4InMisc;
layout (location = 1) in vec2 f2InTexcoord;
layout (location = 2) in vec4 f4InColor;

// Output
layout (location = 0) out vec4 f4OutColor;

float RoundBox(vec2 p, vec2 b, float r)
{
    return length(max(abs(p) - b + r, 0.0)) - r;
}

void main()
{
    if (ui4InMisc.z == 1) // Solid colour
    {
        f4OutColor = f4InColor;
    }
    else if (ui4InMisc.z == 2) // Font
    {
        f4OutColor = vec4(f4InColor.xyz, f4InColor.w * texture(sampler2D(pTextures[ui4InMisc.y], texturesSampler), f2InTexcoord).r);
    }
    else // Texture
    {
        f4OutColor = f4InColor * texture(sampler2D(pTextures[ui4InMisc.y], texturesSampler), f2InTexcoord);

        if (ui4InMisc.z == 3) // Rounded edges
        {
            vec2 iResolution = vec2(1000.0f, 1000.0f);
            float t = 0.01f;
            float iRadius = min(iResolution.x, iResolution.y) * (0.05 + t);
            vec2 halfRes = 0.5 * iResolution.xy;
            float b = RoundBox(1000.0f * f2InTexcoord - halfRes, halfRes, iRadius);
            f4OutColor.a = mix(1.0, 0.0, smoothstep(0.0,1.0,b));
        }
    }
}
