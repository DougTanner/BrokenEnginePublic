#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D textureSampler;
layout (binding = 3) uniform sampler2D noiseTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec2 f2WorldPosition = vec2((1.0f - f2InTexcoord.x) * globalLayout.f4SmokeArea.x + f2InTexcoord.x * globalLayout.f4SmokeArea.z, (1.0f - f2InTexcoord.y) * globalLayout.f4SmokeArea.y + f2InTexcoord.y * globalLayout.f4SmokeArea.w);
	float fElevation = 0.0f;

	vec2 f2Noise = SmokeWindNoise(globalLayout, f2WorldPosition, noiseTextureSampler, globalLayout.f4SmokeThree.y * globalLayout.f4SmokeFour.x, fElevation) +
	               SmokeNoise(globalLayout, f2WorldPosition, noiseTextureSampler, 1.0f, globalLayout.f4SmokeFour.x);
	// f2Noise *= globalLayout.f4SmokeFour.z;
	f2Noise *= 0.5f;

	f4OutColor = globalLayout.f4SmokeOne.w * texture(textureSampler, f2InTexcoord + f2Noise);
}
