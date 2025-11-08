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
	AxisAlignedQuadLayout pQuads[];
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
    const vec2 f2Center = vec2(0.5f, 0.5f);
	vec4 f4Texture = texture(sampler2D(pTextures[int32_t(f4InMisc.x + 0.4f)], texturesSampler), f2Center + Rotate(f2InTexcoord - f2Center, f4InMisc.z));

	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	f4Color.rgb *= f4Color.a;

	vec2 f2Direction = f2InTexcoord - f2Center;
	vec4 f4Direction = vec4(f2Direction.x > 0.0f ? f2Direction.x : 0.0f, f2Direction.x < 0.0f ? -f2Direction.x : 0.0f, f2Direction.y > 0.0f ? f2Direction.y : 0.0f, f2Direction.y < 0.0f ? -f2Direction.y : 0.0f);

	f4OutColorRed = f4Direction * f4InMisc.y * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Direction * f4InMisc.y * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Direction * f4InMisc.y * (f4Color.b * f4Texture.b);
}
