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
layout (binding = 4) uniform sampler2D windTextureSampler;

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

	vec2 f2Noise = SmokeWindNoise(globalLayout, f2WorldPosition, noiseTextureSampler, globalLayout.fSmokeWindNoiseScale * globalLayout.fSmokeNoiseScaleTwo, fElevation) +
	               SmokeNoise(globalLayout, f2WorldPosition, noiseTextureSampler, 1.0f, globalLayout.fSmokeNoiseScaleTwo);
	// f2Noise *= globalLayout.fSmokeObjectHeightInv;
	f2Noise *= 0.5f;

	// Sample wind field and blend between displaced and retained smoke
	vec2 f2WindSample = texture(windTextureSampler, f2InTexcoord).rg;
	float fWindMag = length(f2WindSample);
	if (fWindMag > 1e-10f)
	{
		vec2 f2WindDisplacement = globalLayout.fWindToSmokeStrength * (f2WindSample / fWindMag) * pow(fWindMag, globalLayout.fWindToSmokePower);
		float fWindMask = texture(noiseTextureSampler, globalLayout.fSmokeWindMaskScale * f2WorldPosition.yx).x;
		f2WindDisplacement *= 1.0f - globalLayout.fSmokeWindMaskStrength * fWindMask;
		float fSmokeMoved = texture(textureSampler, f2InTexcoord + f2Noise + f2WindDisplacement).x;
		float fSmokeStayed = texture(textureSampler, f2InTexcoord + f2Noise).x;
		f4OutColor = globalLayout.fSmokeDecay * vec4(mix(fSmokeMoved, fSmokeStayed, globalLayout.fWindSmokeRetention));
	}
	else
	{
		f4OutColor = globalLayout.fSmokeDecay * vec4(texture(textureSampler, f2InTexcoord + f2Noise).x);
	}

	// Extra decay when value is low
	if (f4OutColor.x < globalLayout.fSmokeDecayExtraThreshold)
	{
		f4OutColor.x *= globalLayout.fSmokeDecayExtra;
	}

	// Extra decay over terrain
	if (fElevation > 0.0f)
	{
		f4OutColor.x *= pow(globalLayout.fSmokeDecay, max(1.0f, fElevation - 5.0f));
	}

	// Extra decay at edge of simulation area
	f4OutColor.x *= min(1.0f, globalLayout.fSmokeEdgeDecayDistanceInv * min(f2InTexcoord.x, min(1.0f - f2InTexcoord.x, min(f2InTexcoord.y, 1.0f - f2InTexcoord.y))));
}
