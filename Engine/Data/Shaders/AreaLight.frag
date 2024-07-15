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
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec4 f4Texture = texture(sampler2D(pTextures[int32_t(f4InMisc.x + 0.4f)], texturesSampler), f2InTexcoord);

	float fColor = 0.0f;
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	f4Color.rgb *= f4Color.a;
	if (int(pushConstantsLayout.f4Pipeline.z) == 0)
	{
		fColor = f4Color.r * f4Texture.r;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 1)
	{
		fColor = f4Color.g * f4Texture.g;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 2)
	{
		fColor = f4Color.b * f4Texture.b;
	}
	f4OutColor = f4InMisc.y * fColor * vec4(0.25f, 0.25f, 0.25f, 0.25f);
	f4OutColor *= pQuads[iInInstanceIndex].f4Misc;
}
