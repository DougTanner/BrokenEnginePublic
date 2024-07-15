#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 1) uniform sampler2D textureSampler;
layout (binding = 2) uniform sampler2D noiseTextureSampler;
layout (binding = 3) uniform sampler2D elevationTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	vec2 f2WorldPosition = vec2((1.0f - f2InTexcoord.x) * globalLayout.f4SmokeArea.x + f2InTexcoord.x * globalLayout.f4SmokeArea.z, (1.0f - f2InTexcoord.y) * globalLayout.f4SmokeArea.y + f2InTexcoord.y * globalLayout.f4SmokeArea.w);
	vec2 f2VisibleAreaTexcoord = WorldToVisibleArea(vec3(f2WorldPosition, 0.0f), globalLayout.f4VisibleArea);
	float fElevation = texture(elevationTextureSampler, f2VisibleAreaTexcoord).x;

	vec2 f2Noise = SmokeWindNoise(globalLayout, f2WorldPosition, noiseTextureSampler, globalLayout.f4SmokeThree.y * globalLayout.f4SmokeFour.y, fElevation) +
	               SmokeNoise(globalLayout, f2WorldPosition, noiseTextureSampler, 1.0f, globalLayout.f4SmokeFour.y);
	// f2Noise *= globalLayout.f4SmokeFour.z;
	f2Noise *= 0.5f;

	// Sample from other smoke texture and multiply by decay
	f4OutColor = globalLayout.f4SmokeOne.w * texture(textureSampler, f2InTexcoord + f2Noise);

	// Extra decay when value is low
	if (f4OutColor.x < globalLayout.f4SmokeThree.x)
	{
		f4OutColor.x *= globalLayout.f4SmokeTwo.w;
	}

	// Extra decay over terrain
	if (fElevation > 0.0f)
	{
		f4OutColor.x *= pow(globalLayout.f4SmokeOne.w, max(1.0f, fElevation - 5.0f));
	}

	// Extra decay at edge of simulation area
	f4OutColor.x *= min(1.0f, globalLayout.f4SmokeFour.w * min(f2InTexcoord.x, min(1.0f - f2InTexcoord.x, min(f2InTexcoord.y, 1.0f - f2InTexcoord.y))));
}
