#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform sampler2D windTextureSampler;
layout (binding = 2) uniform sampler2D noiseTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec2 f2WorldPosition = vec2((1.0f - f2InTexcoord.x) * globalLayout.f4SmokeArea.x + f2InTexcoord.x * globalLayout.f4SmokeArea.z, (1.0f - f2InTexcoord.y) * globalLayout.f4SmokeArea.y + f2InTexcoord.y * globalLayout.f4SmokeArea.w);

	// Semi-Lagrangian advection: trace back along wind direction to find source
	vec2 f2Wind = texture(windTextureSampler, f2InTexcoord).rg;
	vec2 f2SourceUV = f2InTexcoord - f2Wind * globalLayout.f4WindOne.x;
	vec2 f2AdvectedWind = texture(windTextureSampler, f2SourceUV).rg;

	// Swirl: perpendicular perturbation via noise
	float fSwirlNoise = texture(noiseTextureSampler, f2WorldPosition * globalLayout.f4WindOne.y).r;
	vec2 f2Perpendicular = vec2(-f2AdvectedWind.y, f2AdvectedWind.x);
	f2AdvectedWind += globalLayout.f4WindOne.z * (fSwirlNoise - 0.5f) * f2Perpendicular;

	// Decay
	f2AdvectedWind *= globalLayout.f4WindOne.w;

	// Edge decay
	float fEdge = min(f2InTexcoord.x, min(1.0f - f2InTexcoord.x, min(f2InTexcoord.y, 1.0f - f2InTexcoord.y)));
	f2AdvectedWind *= min(1.0f, globalLayout.f4WindTwo.x * fEdge);

	// Velocity clamp
	float fVelLen = length(f2AdvectedWind);
	if (fVelLen > globalLayout.f4WindTwo.z)
		f2AdvectedWind *= globalLayout.f4WindTwo.z / fVelLen;

	f4OutColor = vec4(f2AdvectedWind, 0.0f, 1.0f);
}
