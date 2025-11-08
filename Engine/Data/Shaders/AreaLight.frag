#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (binding = 1) buffer readonly quadsUniform
{
	QuadLayout pQuads[];
};

layout (binding = 2) uniform sampler texturesSampler;
layout (binding = 3) uniform texture2D pTextures[kiTextureCount];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	vec4 f4Texture = texture(sampler2D(pTextures[int32_t(f4InMisc.x + 0.4f)], texturesSampler), f2InTexcoord);

	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	f4Color.rgb *= f4Color.a;

	f4OutColorRed = f4InMisc.y * (f4Color.r * f4Texture.r) * vec4(0.25f, 0.25f, 0.25f, 0.25f) * pQuads[iInInstanceIndex].f4Misc;
	f4OutColorGreen = f4InMisc.y * (f4Color.g * f4Texture.g) * vec4(0.25f, 0.25f, 0.25f, 0.25f) * pQuads[iInInstanceIndex].f4Misc;
	f4OutColorBlue = f4InMisc.y * (f4Color.b * f4Texture.b) * vec4(0.25f, 0.25f, 0.25f, 0.25f) * pQuads[iInInstanceIndex].f4Misc;
}
