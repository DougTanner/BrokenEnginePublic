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
layout (location = 3) in vec2 f2InQuadPosition;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	vec4 f4Texture = texture(sampler2D(pTextures[nonuniformEXT(int32_t(f4InMisc.x + 0.4f))], texturesSampler), f2InTexcoord);

	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	f4Color.rgb *= f4Color.a;

	// Calculate normalized direction from quad center
	vec4 f4Direction = CalculateDirectionalLight(f2InQuadPosition);

	// Apply directional lighting with per-quad multipliers
	vec4 f4DirectionMultipliers = pQuads[iInInstanceIndex].f4Misc;
	f4OutColorRed = f4Direction * f4DirectionMultipliers * f4InMisc.y * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Direction * f4DirectionMultipliers * f4InMisc.y * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Direction * f4DirectionMultipliers * f4InMisc.y * (f4Color.b * f4Texture.b);
}
