#version 460

#include "ShaderLayouts.h"

// Vorticity confinement mode (uncomment exactly one):
// #define VORTICITY_NONE
// #define VORTICITY_SIMPLE
#define VORTICITY_PROPER

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

	float fAdvectionScale = mix(globalLayout.fWindAdvectionScaleLow, globalLayout.fWindAdvectionScaleHigh, fMagFactor);
	vec2 f2SourceUV = f2InTexcoord - f2Wind * fAdvectionScale * fSpread * fTimeScale;
	vec2 f2AdvectedWind = texture(windTextureSampler, f2SourceUV).rg;

	// Swirl: perpendicular perturbation via noise
	float fSwirlScale = mix(globalLayout.fWindSwirlScaleLow, globalLayout.fWindSwirlScaleHigh, fMagFactor);
	float fSwirlSpeed = mix(globalLayout.fWindSwirlSpeedLow, globalLayout.fWindSwirlSpeedHigh, fMagFactor);
	float fSwirlAmount = mix(globalLayout.fWindSwirlAmountLow, globalLayout.fWindSwirlAmountHigh, fMagFactor);
	float fSwirlNoise = texture(noiseTextureSampler, f2WorldPosition * fSwirlScale + vec2(globalLayout.fWindTime * fSwirlSpeed)).r;
	vec2 f2Perpendicular = vec2(-f2AdvectedWind.y, f2AdvectedWind.x);
	f2AdvectedWind += fSwirlAmount * fSpread * fTimeScale * (fSwirlNoise - 0.5f) * f2Perpendicular;

	// Neighbor reads (shared by vorticity and diffusion)
	float h = fTexelSize;
	vec2 f2Right = texture(windTextureSampler, f2InTexcoord + vec2(h, 0.0f)).rg;
	vec2 f2Left  = texture(windTextureSampler, f2InTexcoord - vec2(h, 0.0f)).rg;
	vec2 f2Up    = texture(windTextureSampler, f2InTexcoord + vec2(0.0f, h)).rg;
	vec2 f2Down  = texture(windTextureSampler, f2InTexcoord - vec2(0.0f, h)).rg;

#ifdef VORTICITY_SIMPLE
	// Vorticity confinement (simple: perpendicular to wind direction)
	float fVorticityConfinement = mix(globalLayout.fWindVorticityConfinementLow, globalLayout.fWindVorticityConfinementHigh, fMagFactor);
	if (fVorticityConfinement > 0.0f)
	{
		float fOmega = f2Right.y - f2Left.y - f2Up.x + f2Down.x;

		float fAdvectedMag = length(f2AdvectedWind);
		if (fAdvectedMag > 1e-6f)
		{
			vec2 f2Dir = f2AdvectedWind / fAdvectedMag;
			vec2 f2PerpConfinement = vec2(-f2Dir.y, f2Dir.x);
			f2AdvectedWind += fVorticityConfinement * fOmega * fTimeScale * f2PerpConfinement;
		}
	}
#endif

#ifdef VORTICITY_PROPER
	// Vorticity confinement (proper Fedkiw/Steinhoff: gradient of vorticity magnitude)
	float fVorticityConfinement = mix(globalLayout.fWindVorticityConfinementLow, globalLayout.fWindVorticityConfinementHigh, fMagFactor);
	if (fVorticityConfinement > 0.0f)
	{
		float fOmegaCenter = f2Right.y - f2Left.y - f2Up.x + f2Down.x;

		// 8 additional texture reads for extended stencil
		vec2 f2RightUp    = texture(windTextureSampler, f2InTexcoord + vec2(h, h)).rg;
		vec2 f2RightDown  = texture(windTextureSampler, f2InTexcoord + vec2(h, -h)).rg;
		vec2 f2LeftUp     = texture(windTextureSampler, f2InTexcoord + vec2(-h, h)).rg;
		vec2 f2LeftDown   = texture(windTextureSampler, f2InTexcoord + vec2(-h, -h)).rg;
		vec2 f2Right2     = texture(windTextureSampler, f2InTexcoord + vec2(2.0f * h, 0.0f)).rg;
		vec2 f2Left2      = texture(windTextureSampler, f2InTexcoord - vec2(2.0f * h, 0.0f)).rg;
		vec2 f2Up2        = texture(windTextureSampler, f2InTexcoord + vec2(0.0f, 2.0f * h)).rg;
		vec2 f2Down2      = texture(windTextureSampler, f2InTexcoord - vec2(0.0f, 2.0f * h)).rg;

		// Omega at cardinal neighbors (centered differences using center pixel)
		float fOmegaRight = f2Right2.y - f2Wind.y - f2RightUp.x + f2RightDown.x;
		float fOmegaLeft  = f2Wind.y - f2Left2.y - f2LeftUp.x + f2LeftDown.x;
		float fOmegaUp    = f2RightUp.y - f2LeftUp.y - f2Up2.x + f2Wind.x;
		float fOmegaDown  = f2RightDown.y - f2LeftDown.y - f2Wind.x + f2Down2.x;

		// Gradient of |omega|
		vec2 f2Eta = vec2(abs(fOmegaRight) - abs(fOmegaLeft), abs(fOmegaUp) - abs(fOmegaDown));
		float fEtaLen = length(f2Eta);
		if (fEtaLen > 1e-6f)
		{
			vec2 f2N = f2Eta / fEtaLen;
			// 2D cross product: N x omega_z
			vec2 f2Force = vec2(f2N.y, -f2N.x) * fOmegaCenter;
			f2AdvectedWind += fVorticityConfinement * f2Force * fTimeScale;
		}
	}
#endif

	// Diffusion: average with 4 neighbors for lateral spread
	float fDiffusion = mix(globalLayout.fWindDiffusionLow, globalLayout.fWindDiffusionHigh, fMagFactor);
	if (fDiffusion > 0.0f)
	{
		vec2 f2Avg = 0.25f * (f2Right + f2Left + f2Up + f2Down);
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
