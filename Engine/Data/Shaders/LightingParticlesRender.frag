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

layout (binding = 2) buffer readonly particlesUniform
{
	ParticlesLayout particles;
};

layout (binding = 3) uniform sampler2D cookieSamplers[kiParticlesCookieCount];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColorRed;
layout (location = 1) out vec4 f4OutColorGreen;
layout (location = 2) out vec4 f4OutColorBlue;

void main()
{
	int i = iInInstanceIndex;
	float fIntensity = particles.pParticles[i].f4MiscTwo.z;
	// fIntensity = pow(max(fIntensity, 0.0f), particles.pParticles[i].f4MiscTwo.w);

	vec4 f4Color = unpackUnorm4x8(particles.pParticles[i].i4Misc.x).abgr;
	float fCookie = texture(cookieSamplers[nonuniformEXT(particles.pParticles[i].i4Misc.y)], f2InTexcoord).x;
	f4Color = vec4(fCookie * fIntensity * f4Color.w * f4Color.xyz, 0.0f);

	// Compute all color channels simultaneously
	float fParticleIntensity = float(particles.pParticles[i].i4Misc.z);
	f4OutColorRed = f4Color.r * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;
	f4OutColorGreen = f4Color.g * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;
	f4OutColorBlue = f4Color.b * vec4(0.25f, 0.25f, 0.25f, 0.25f) * fParticleIntensity;
}
