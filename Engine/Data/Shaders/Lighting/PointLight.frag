#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 1) buffer readonly quadsUniform
{
	AxisAlignedQuadLayout pQuads[];
};

layout (set = 0, binding = 12) uniform sampler texturesSampler;
layout (set = 0, binding = 4) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
    const vec2 f2Center = vec2(0.5f, 0.5f);
	vec4 f4Texture = texture(sampler2D(pTextures[nonuniformEXT(int32_t(f4InParams.x + 0.4f))], texturesSampler), f2Center + Rotate(f2InTexcoord - f2Center, f4InParams.z));

	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	float fAlpha = f4Color.a * f4Texture.a;

	// Calculate normalized direction from quad center
	vec4 f4Direction = CalculateDirectionalLight(f2InTexcoord);

	f4OutColorRed = f4Direction * f4InParams.y * fAlpha * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Direction * f4InParams.y * fAlpha * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Direction * f4InParams.y * fAlpha * (f4Color.b * f4Texture.b);
}
