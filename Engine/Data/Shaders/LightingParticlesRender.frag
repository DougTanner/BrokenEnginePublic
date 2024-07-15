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

layout (binding = 2) buffer readonly particlesUniform
{
	ParticlesLayout particles;
};

layout (binding = 3) uniform sampler2D cookieSamplers[kiParticlesCookieCount];

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 fOutColor;

void main()
{
	int i = iInInstanceIndex;
	float fIntensity = particles.pParticles[i].f4MiscTwo.z;
	// fIntensity = pow(max(fIntensity, 0.0f), particles.pParticles[i].f4MiscTwo.w);

	vec4 f4Color = unpackUnorm4x8(particles.pParticles[i].i4Misc.x).abgr;
	float fCookie = texture(cookieSamplers[particles.pParticles[i].i4Misc.y], f2InTexcoord).x;
	f4Color = vec4(fCookie * fIntensity * f4Color.w * f4Color.xyz, 0.0f);

	float fColor = 0.0f;
	if (int(pushConstantsLayout.f4Pipeline.z) == 0)
	{
		fColor = f4Color.r;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 1)
	{
		fColor = f4Color.g;
	}
	else if (int(pushConstantsLayout.f4Pipeline.z) == 2)
	{
		fColor = f4Color.b;
	}
	fOutColor = fColor * vec4(0.25f, 0.25f, 0.25f, 0.25f);

	// Intensity
	fOutColor *= float(particles.pParticles[i].i4Misc.z);
}
