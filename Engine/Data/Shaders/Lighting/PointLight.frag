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
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 1) buffer readonly quadsUniform
{
	AxisAlignedQuadLayout pQuads[];
};

layout (set = 0, binding = kiGlobalBindingSamplerClamp) uniform sampler texturesSampler;
layout (set = 0, binding = kiGlobalBindingBindlessTextures) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InParams;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 4) in vec2 f2InWorldPosition;
layout (location = 5) in flat vec2 f2InWorldCenter;
// Match QuadsAxisAlignedVisibleArea.vert output so SPIR-V interface validation passes; not read here.
layout (location = 7) in flat uint uiInTextureSlotUnused;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	// Compute all color channels simultaneously
	vec4 f4Color = unpackUnorm4x8(pQuads[iInInstanceIndex].uiColor).abgr;

	// Early-out before the bindless sample: a zero per-quad alpha forces fAlpha to 0 regardless of the texture
	if (f4Color.a < 0.001f)
		discard;

	const vec2 f2Center = vec2(0.5f, 0.5f);
	vec4 f4Texture = texture(sampler2D(pTextures[nonuniformEXT(int32_t(f4InParams.x + 0.4f))], texturesSampler), f2Center + Rotate(f2InTexcoord - f2Center, f4InParams.z));
	float fAlpha = f4Color.a * f4Texture.a;

#if 1 // defined(ENABLE_DIRECTIONAL_DEPOSIT)
	vec2 f2Dir = f2InWorldPosition - f2InWorldCenter;
	float fDist = length(f2Dir);
	vec4 f4Direction;
	if (fDist > 1e-4f)
	{
		vec2 f2NormDir = f2Dir / fDist;
#if 0 // Omnidirectional: equal deposit, spread handles directionality
		f4Direction = vec4(0.25f);
#elif 1 // Cosine-lobe: smooth cos^2 falloff at axis boundaries
		f4Direction = vec4(f2NormDir.x * f2NormDir.x * step(0.0f, -f2NormDir.x),
		                   f2NormDir.x * f2NormDir.x * step(0.0f, f2NormDir.x),
		                   f2NormDir.y * f2NormDir.y * step(0.0f, -f2NormDir.y),
		                   f2NormDir.y * f2NormDir.y * step(0.0f, f2NormDir.y));
#else // Hard clamp: binary split at EWNS axes
		f4Direction = vec4(max(-f2NormDir.x, 0.0f), max(f2NormDir.x, 0.0f), max(-f2NormDir.y, 0.0f), max(f2NormDir.y, 0.0f));
#endif
	}
	else
	{
		f4Direction = vec4(0.25f);
	}

#else
	vec4 f4Direction = vec4(0.25f);
#endif

	if (fAlpha < 0.001f)
		discard;

	float fEdgeFade = LightingDepositEdgeFade(gl_FragCoord.xy, globalLayout.f2LightingDepositSizeInv);
	vec4 f4Base = f4Direction * (f4InParams.y * fAlpha * fEdgeFade);
	f4OutColorRed = f4Base * (f4Color.r * f4Texture.r);
	f4OutColorGreen = f4Base * (f4Color.g * f4Texture.g);
	f4OutColorBlue = f4Base * (f4Color.b * f4Texture.b);
}
