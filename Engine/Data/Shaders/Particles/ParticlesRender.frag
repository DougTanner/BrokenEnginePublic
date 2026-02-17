#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (scalar, set = 1, binding = 2) buffer readonly renderUniform
{
	ParticlesLayout render;
};

layout (set = 1, binding = 3) uniform sampler2D smokeSampler;

layout (set = 0, binding = 12) uniform sampler particleSampler;
layout (set = 0, binding = 4) uniform texture2D pTextures[];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InWorldPosition;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	int i = iInInstanceIndex;
	float fIntensity = render.pParticles[i].fIntensity;
	fIntensity = pow(fIntensity, render.pParticles[i].fIntensityPower);

	vec4 f4Color = unpackUnorm4x8(render.pParticles[i].iColor).abgr;
	float fCookie = texture(sampler2D(pTextures[nonuniformEXT(render.pParticles[i].iCookie)], particleSampler), f2InTexcoord).x;
	f4OutColor = vec4(fCookie * fIntensity * f4Color.w * f4Color.xyz, 0.0f);

	float fHeightFraction = clamp((f3InWorldPosition.z - globalLayout.fBaseHeight) * globalLayout.fSmokeObjectHeightInv, 0.0f, 1.0f);
	float fSmokeFade = 1.0f - fHeightFraction * fHeightFraction;
	f4OutColor.xyz *= SmokeShadow(globalLayout, f3InWorldPosition, smokeSampler, fSmokeFade);
}
