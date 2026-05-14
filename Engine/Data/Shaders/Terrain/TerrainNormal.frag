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
layout (location = 6) in flat vec2 f2InRotationCosSin;
layout (location = 7) in flat uint uiInTextureSlot;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	// BC5 source: only RG stored, in standard normal-map convention (encoded=0.5 -> 0,
	// encoded=1 -> +1). G-buffer round-trips XY through the same convention (re-encoded after
	// rotation) so Terrain.frag's `2x - 1` decode recovers the rotated tangent; B carries the
	// reconstructed Z magnitude in [0,1] since downstream uses it without decoding.
	vec2 f2RG = texture(textureSampler[nonuniformEXT(uiInTextureSlot)], f2InTexcoord).rg;

	// Per-island normal rotation: rotate sampled tangent (X, Y) by (cos, sin) precomputed in vert shader.
	// Z is rotation-invariant since rotation is around the surface up axis.
	vec2 f2XY = 2.0f * f2RG - 1.0f;
	float fCos = f2InRotationCosSin.x;
	float fSin = f2InRotationCosSin.y;
	vec2 f2Rot = vec2(f2XY.x * fCos - f2XY.y * fSin,
	                  f2XY.x * fSin + f2XY.y * fCos);
	float fZ = sqrt(clamp(1.0f - dot(f2Rot, f2Rot), 0.0f, 1.0f));
	vec2 f2EncodedRG = 0.5f + 0.5f * f2Rot;
	f4OutColor = vec4(f2EncodedRG, fZ, 1.0f);
}
