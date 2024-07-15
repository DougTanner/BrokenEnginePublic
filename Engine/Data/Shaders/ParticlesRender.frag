#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 2) buffer readonly renderUniform
{
	ParticlesLayout render;
};

layout (binding = 3) uniform sampler2D cookieSamplers[kiParticlesCookieCount];

// layout (binding = 4) uniform sampler2D smokeSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec3 f3InWorldPosition;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	int i = iInInstanceIndex;
	float fIntensity = render.pParticles[i].f4MiscTwo.z;
	fIntensity = pow(fIntensity, render.pParticles[i].f4MiscTwo.w);

	vec4 f4Color = unpackUnorm4x8(render.pParticles[i].i4Misc.x).abgr;
	float fCookie = texture(cookieSamplers[render.pParticles[i].i4Misc.y], f2InTexcoord).x;
    // Water particle? f4OutColor.xyz = AddSmokeToObject(globalLayout, mainLayout, smokeSampler, f4OutColor.xyz, f3InWorldPosition.xyz, 1.0f);
	f4OutColor = vec4(fCookie * fIntensity * f4Color.w * f4Color.xyz, 0.0f);
}
