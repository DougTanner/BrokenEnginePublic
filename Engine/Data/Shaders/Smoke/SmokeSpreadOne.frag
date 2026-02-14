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
layout (binding = 4) uniform sampler2D windTextureSamplerOne;
layout (binding = 5) uniform sampler2D windTextureSamplerTwo;

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

	vec2 f2Noise = SmokeWindNoise(globalLayout, f2WorldPosition, noiseTextureSampler, globalLayout.fSmokeWindNoiseScale * globalLayout.fSmokeNoiseScaleOne, fElevation) +
	               SmokeNoise(globalLayout, f2WorldPosition, noiseTextureSampler, 1.0f, globalLayout.fSmokeNoiseScaleOne);
	// f2Noise *= globalLayout.fSmokeObjectHeightInv;
	f2Noise *= 0.5f;

	// Sample wind field and blend between displaced and retained smoke
	vec2 f2WindSample = mix(
		texture(windTextureSamplerOne, f2InTexcoord).rg,
		texture(windTextureSamplerTwo, f2InTexcoord).rg,
		globalLayout.fWindTextureIndex);
	float fWindMag = length(f2WindSample);
	if (fWindMag > 1e-10f)
	{
		vec2 f2WindDisplacement = globalLayout.fWindToSmokeStrength * (f2WindSample / fWindMag) * pow(fWindMag, globalLayout.fWindToSmokePower);
		float fSmokeMoved = texture(textureSampler, f2InTexcoord + f2Noise + f2WindDisplacement).x;
		float fSmokeStayed = texture(textureSampler, f2InTexcoord + f2Noise).x;
		f4OutColor = globalLayout.fSmokeDecay * vec4(mix(fSmokeMoved, fSmokeStayed, globalLayout.fWindSmokeRetention));
	}
	else
	{
		f4OutColor = globalLayout.fSmokeDecay * vec4(texture(textureSampler, f2InTexcoord + f2Noise).x);
	}
}
