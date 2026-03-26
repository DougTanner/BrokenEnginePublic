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

layout (set = 1, binding = 2) buffer lightOccupancyBuffer
{
	uint occupancy[];
};

layout (set = 0, binding = 3) uniform sampler texturesSampler;
layout (set = 0, binding = 4) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 4) in vec2 f2InWorldPosition;
layout (location = 5) in flat vec2 f2InWorldCenter;

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
	// Calculate EWNS directional weights from world-space direction (interpolated varyings)
	vec2 f2WorldDir = f2InWorldPosition - f2InWorldCenter;
	vec2 f2AbsDir = abs(f2WorldDir);
	float fMaxDist = max(f2AbsDir.x, f2AbsDir.y);
	vec2 f2NormDir = fMaxDist > 0.0f ? f2WorldDir / fMaxDist : vec2(0.0f);
	vec4 f4Direction = vec4(max(f2NormDir.x, 0.0f), max(-f2NormDir.x, 0.0f), max(f2NormDir.y, 0.0f), max(-f2NormDir.y, 0.0f));

	// Energy normalization: blend between Chebyshev (sum varies) and normalized (sum = 1)
	float fDirSum = f4Direction.x + f4Direction.y + f4Direction.z + f4Direction.w;
	if (fDirSum > 0.0f)
		f4Direction = mix(f4Direction, f4Direction / fDirSum, globalLayout.fDepositEnergyNormalize);
#else
	vec4 f4Direction = vec4(0.25f);
#endif

	// Rectangular falloff: full intensity in interior, fades to zero at quad edges
	vec2 f2Edge = abs(f2InTexcoord - vec2(0.5f)) * 2.0f;
	float fFalloff = (1.0f - smoothstep(0.5f, 1.0f, f2Edge.x)) * (1.0f - smoothstep(0.5f, 1.0f, f2Edge.y));

	if (fAlpha * fFalloff < 0.001f)
		discard;

	vec4 f4Base = f4Direction * (f4InParams.y * fAlpha * fFalloff);
	f4OutColorRed = f4Base * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Base * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Base * (f4Color.b * f4Texture.b);

	// Mark occupancy (deposit-texture-space tiles)
	uint uiTileX = uint(gl_FragCoord.x) / uint(kiComputeTileSize);
	uint uiTileY = uint(gl_FragCoord.y) / uint(kiComputeTileSize);
	uint uiTileIndex = uiTileY * globalLayout.uiLightTilesX + uiTileX;
	atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
}
