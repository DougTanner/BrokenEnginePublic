#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
    GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly visibleLightsUniform
{
	VisibleLightQuadLayout pQuads[];
};

layout (set = 1, binding = 5) uniform sampler2D elevationTextureSampler;
layout (set = 0, binding = kiGlobalBindingSamplerRepeat) uniform sampler texturesSampler;
layout (set = 0, binding = kiGlobalBindingBindlessTextures) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InPosition;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in vec4 f4InColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fTerrainElevation = texture(elevationTextureSampler, WorldToVisibleArea(f3InPosition, globalLayout.f4VisibleArea)).x;
	const float fBaseHeight = globalLayout.fBaseHeight;
	const float fFalloff = 0.25f * fBaseHeight;
	float fHeightPercent = 1.0f;
	if (fTerrainElevation > fBaseHeight)
	{
		fHeightPercent = 1.0f - clamp((fTerrainElevation - fBaseHeight) / fFalloff, 0.0f, 1.0f);
	}

	vec4 f4Color = f4InColor;

    const vec2 f2Center = vec2(0.5f, 0.5f);
	vec2 f2Texcoord = f2InTexcoord;
	if (pQuads[iInInstanceIndex].fRotation != 0.0f)
	{
		f2Texcoord = clamp(f2Center + Rotate(f2InTexcoord - f2Center, pQuads[iInInstanceIndex].fRotation), vec2(0.0f, 0.0f), vec2(1.0f, 1.0f));
	}
	vec4 f4Texture = texture(sampler2D(pTextures[nonuniformEXT(pQuads[iInInstanceIndex].uiTextureIndex)], texturesSampler), f2Texcoord);
	f4OutColor = vec4(pQuads[iInInstanceIndex].fIntensity * f4Color.rgb * f4Texture.rgb,
	                  f4Color.a * f4Texture.a * fHeightPercent);
}
