#version 460

#include "ShaderLayouts.h"

// Uniforms
layout (binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (binding = 2) uniform sampler2D windTextureSampler;
layout (binding = 3) uniform sampler2D noiseTextureSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec4 f4InMisc;
layout (location = 2) in vec2 f2InTexcoord;
layout (location = 3) in flat uint uiInColor;

// Output
layout (location = 0) out vec4 f4OutColor;

void main()
{
	float fTexelSize = globalLayout.fWindTexelSize;
	float fTimeScale = globalLayout.fWindTimeScale;

	// World position for noise sampling
	vec2 f2WorldPosition = vec2(
		(1.0f - f2InTexcoord.x) * globalLayout.f4SmokeArea.x + f2InTexcoord.x * globalLayout.f4SmokeArea.z,
		(1.0f - f2InTexcoord.y) * globalLayout.f4SmokeArea.y + f2InTexcoord.y * globalLayout.f4SmokeArea.w);

	// Semi-Lagrangian advection: trace back along wind direction to find source
	vec2 f2Wind = texture(windTextureSampler, f2InTexcoord).rg;

	// Magnitude-dependent behavior: weak wind is laminar, strong wind is turbulent
	float fMag = length(f2Wind);
	float fMagFactor = clamp((fMag - globalLayout.fWindThresholdLow) / max(globalLayout.fWindThresholdHigh - globalLayout.fWindThresholdLow, 0.001), 0.0, 1.0);
	fMagFactor = pow(fMagFactor, globalLayout.fWindThresholdPower);

	// Momentum: slider up = more momentum = less spread/swirl/diffusion
	float fSpread = 1.0 - mix(globalLayout.fWindMomentumLow, globalLayout.fWindMomentumHigh, fMagFactor);

	vec2 f2SourceUV = f2InTexcoord - f2Wind * globalLayout.fWindAdvectionScale * fSpread * fTimeScale;
	vec2 f2AdvectedWind = texture(windTextureSampler, f2SourceUV).rg;

	// Swirl: perpendicular perturbation via noise
	float fSwirlNoise = texture(noiseTextureSampler, f2WorldPosition * globalLayout.fWindSwirlScale).r;
	vec2 f2Perpendicular = vec2(-f2AdvectedWind.y, f2AdvectedWind.x);
	f2AdvectedWind += globalLayout.fWindSwirlAmount * fSpread * fTimeScale * (fSwirlNoise - 0.5f) * f2Perpendicular;

	// Diffusion: average with 4 neighbors for lateral spread
	float fDiffusion = globalLayout.fWindDiffusion;
	if (fDiffusion > 0.0f)
	{
		float h = fTexelSize;
		vec2 f2Avg = 0.25f * (
			texture(windTextureSampler, f2InTexcoord + vec2(h, 0.0f)).rg +
			texture(windTextureSampler, f2InTexcoord - vec2(h, 0.0f)).rg +
			texture(windTextureSampler, f2InTexcoord + vec2(0.0f, h)).rg +
			texture(windTextureSampler, f2InTexcoord - vec2(0.0f, h)).rg);
		f2AdvectedWind = mix(f2AdvectedWind, f2Avg, min(1.0f, fDiffusion * fSpread * fTimeScale));
	}

	// Decay: slider up = more decay, so invert (1.0 - value) for the pow base
	float fDecayRate = 1.0 - mix(globalLayout.fWindDecayLow, globalLayout.fWindDecayHigh, fMagFactor);
	f2AdvectedWind *= pow(fDecayRate, fTimeScale);

	float fWindMag = length(f2AdvectedWind);
	if (fWindMag > 1e-10f)
	{
		f4OutColor = vec4(f2AdvectedWind, 0.0f, 1.0f);
	}
	else
	{
		f4OutColor = vec4(0.0f, 0.0f, 0.0f, 1.0f);
	}
}
