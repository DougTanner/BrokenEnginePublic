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
	QuadLayout pQuads[];
};

layout (set = 1, binding = 2) buffer lightOccupancyBuffer { uint occupancy[]; };

layout (set = 0, binding = 3) uniform sampler texturesSampler;
layout (set = 0, binding = 4) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in vec2 f2InQuadPosition;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	vec4 f4Texture = texture(sampler2D(pTextures[nonuniformEXT(int32_t(f4InParams.x + 0.4f))], texturesSampler), f2InTexcoord);

	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;
	float fAlpha = f4Color.a * f4Texture.a;

#if 1 // defined(ENABLE_DIRECTIONAL_DEPOSIT)
	// Calculate EWNS directional weights from world-space direction (not local texcoord space)
	vec2 f2V0 = pQuads[iInInstanceIndex].pf4VerticesTexcoords[0].xy;
	vec2 f2V1 = pQuads[iInInstanceIndex].pf4VerticesTexcoords[1].xy;
	vec2 f2V2 = pQuads[iInInstanceIndex].pf4VerticesTexcoords[2].xy;
	vec2 f2V3 = pQuads[iInInstanceIndex].pf4VerticesTexcoords[3].xy;
	vec2 f2WorldCenter = (f2V0 + f2V1 + f2V2 + f2V3) * 0.25f;
	vec2 f2WorldPos = mix(mix(f2V0, f2V1, f2InQuadPosition.x), mix(f2V2, f2V3, f2InQuadPosition.x), f2InQuadPosition.y);
	vec2 f2WorldDir = f2WorldPos - f2WorldCenter;
	vec2 f2AbsDir = abs(f2WorldDir);
	float fMaxDist = max(f2AbsDir.x, f2AbsDir.y);
	vec2 f2NormDir = fMaxDist > 0.0f ? f2WorldDir / fMaxDist : vec2(0.0f);
	vec4 f4Direction = vec4(max(f2NormDir.x, 0.0f), max(-f2NormDir.x, 0.0f), max(-f2NormDir.y, 0.0f), max(f2NormDir.y, 0.0f));
#else
	vec4 f4Direction = vec4(0.25f);
#endif

	// Rectangular falloff: full intensity in interior, fades to zero at quad edges
	vec2 f2Edge = abs(f2InTexcoord - vec2(0.5f)) * 2.0f;
	float fFalloff = (1.0f - smoothstep(0.5f, 1.0f, f2Edge.x)) * (1.0f - smoothstep(0.5f, 1.0f, f2Edge.y));

	f4OutColorRed = f4Direction * f4InParams.y * fAlpha * fFalloff * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Direction * f4InParams.y * fAlpha * fFalloff * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Direction * f4InParams.y * fAlpha * fFalloff * (f4Color.b * f4Texture.b);

	// Mark occupancy for deposited tile and surrounding tiles within dilation radius
	int iCenterTileX = int(gl_FragCoord.x) / int(kiComputeTileSize);
	int iCenterTileY = int(gl_FragCoord.y) / int(kiComputeTileSize);
	int iDilation = int(globalLayout.uiLightOccupancyDilation);
	for (int iDy = -iDilation; iDy <= iDilation; ++iDy)
	{
		for (int iDx = -iDilation; iDx <= iDilation; ++iDx)
		{
			int iTileX = iCenterTileX + iDx;
			int iTileY = iCenterTileY + iDy;
			if (iTileX >= 0 && iTileY >= 0 && uint(iTileX) < globalLayout.uiLightTilesX && uint(iTileY) < globalLayout.uiLightTilesY)
			{
				uint uiTileIndex = uint(iTileY) * globalLayout.uiLightTilesX + uint(iTileX);
				atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
			}
		}
	}
}
