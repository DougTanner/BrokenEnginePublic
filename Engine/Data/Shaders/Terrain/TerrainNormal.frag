#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 1, binding = 2) uniform sampler2D textureSampler[kiMaxIslands];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	// BC5 source: only RG stored. G-buffer write is asymmetric — RG passes through encoded ([0,1] pre-decode) so Terrain.frag can apply its existing `1 - 2x` sign-inverted decode, while B carries the reconstructed (already-decoded) Z magnitude in [0,1] since downstream uses Z without decoding.
	vec2 f2RG = texture(textureSampler[nonuniformEXT(iInInstanceIndex)], f2InTexcoord).rg;

	// Per-island normal flip: 1.0 - n maps (0.5 + 0.5*x) to (0.5 - 0.5*x), negating the decoded direction
	if (f4InMisc.y > 0.5f) // flipX
		f2RG.x = 1.0f - f2RG.x;
	if (f4InMisc.z > 0.5f) // flipY
		f2RG.y = 1.0f - f2RG.y;

	vec2 f2XY = f2RG * 2.0f - 1.0f;
	float fZ = sqrt(clamp(1.0f - dot(f2XY, f2XY), 0.0f, 1.0f));
	f4OutColor = vec4(f2RG, fZ, 1.0f);
}
