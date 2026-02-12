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
layout (binding = 4) uniform sampler2D windTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in flat uint uiInColor;

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

	// Sample wind field and add to smoke movement
	vec2 f2WindOffset = globalLayout.f4WindTwo.y * texture(windTextureSampler, f2InTexcoord).rg;
	float fWindLen = length(f2WindOffset);
	if (fWindLen > globalLayout.f4WindTwo.w)
		f2WindOffset *= globalLayout.f4WindTwo.w / fWindLen;
	f2Noise += f2WindOffset;

	f4OutColor = globalLayout.f4SmokeOne.w * texture(textureSampler, f2InTexcoord + f2Noise);
}
