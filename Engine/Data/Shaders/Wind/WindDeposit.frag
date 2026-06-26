#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 1, binding = 2) uniform sampler2D textureSampler;
layout (set = 1, binding = 3) buffer windOccupancyBuffer { uint occupancy[]; };

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;
// Match QuadsAxisAlignedVisibleArea.vert output so SPIR-V interface validation passes; not read here.
layout (location = 7) in flat uint uiInTextureSlotUnused;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fFalloff = texture(textureSampler, f2InTexcoord).r;
	float fMagnitude = f4InParams.x;

	vec2 f2WindDir;
	if (f4InParams.w > 0.5)
	{
		// Radial: compute outward direction from quad center
		vec2 f2FromCenter = f2InTexcoord - vec2(0.5);
		float fLen = length(f2FromCenter);
		f2WindDir = fLen > 0.001 ? f2FromCenter / fLen : vec2(0.0);
		f2WindDir.y = -f2WindDir.y;  // Texcoord Y is inverted relative to world Y
	}
	else
	{
		// Directional: use CPU-computed direction
		f2WindDir = f4InParams.yz;
	}

	f4OutColor = vec4(fFalloff * fMagnitude * f2WindDir, 0.0, 1.0);

	// Mark occupancy for deposited tiles (seeds hierarchical dispatch)
	if (dot(f4OutColor.rg, f4OutColor.rg) > 0.0f)
	{
		ivec2 i2TileCoord = ivec2(gl_FragCoord.xy) / kiComputeTileSize;
		uint uiTileIndex = i2TileCoord.y * globalLayout.uiWindTilesX + i2TileCoord.x;
		atomicOr(occupancy[uiTileIndex >> 5], 1u << (uiTileIndex & 31));
	}
}
