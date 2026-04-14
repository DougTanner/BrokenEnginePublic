#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = 0) uniform globalUniform
{
	GlobalLayout globalLayout;
};

layout (set = 1, binding = 1) uniform sampler2D redSampler;
layout (set = 1, binding = 2) uniform sampler2D greenSampler;
layout (set = 1, binding = 3) uniform sampler2D blueSampler;
layout (set = 1, binding = 4) uniform sampler2D elevationSampler;

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutRed;
layout (location = 1) out vec4 f4OutGreen;
layout (location = 2) out vec4 f4OutBlue;

void main()
{
	// Per-pass interpolation: t=0 at pass 0 (Start values), t=1 at last pass (End values)
	float fPassIndex = pushConstantsLayout.f4Pipeline.z;
	float fT = fPassIndex / max(globalLayout.fSpreadPassCount - 1.0f, 1.0f);

	// Interpolate spread parameters between Start and End
	float fDirectionality = mix(globalLayout.fSpreadDirectionalityStart, globalLayout.fSpreadDirectionalityEnd, fT);
	float fSpreadDistance = mix(globalLayout.fSpreadDistanceStart, globalLayout.fSpreadDistanceEnd, fT);
	float fDirectionCountBase = mix(globalLayout.fSpreadDirectionCountStart, globalLayout.fSpreadDirectionCountEnd, fT);
	float fRingCountF = mix(globalLayout.fSpreadRingCountStart, globalLayout.fSpreadRingCountEnd, fT);
	float fJitter = mix(globalLayout.fSpreadJitterStart, globalLayout.fSpreadJitterEnd, fT);
	float fDecay = mix(globalLayout.fSpreadDecayStart, globalLayout.fSpreadDecayEnd, fT);
	float fAccumulationDecay = mix(globalLayout.fSpreadAccumulationDecayStart, globalLayout.fSpreadAccumulationDecayEnd, fT);
	float fDistanceFalloff = mix(globalLayout.fSpreadDistanceFalloffStart, globalLayout.fSpreadDistanceFalloffEnd, fT);

	// Height-aware attenuation: convert lighting texcoord to world position, then to visible area texcoord
	vec2 f2WorldPos = vec2(globalLayout.f4LightingArea.x + f2InTexcoord.x * (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x),
		                   globalLayout.f4LightingArea.w + (1.0f - f2InTexcoord.y) * (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w));
	vec2 f2ElevTexcoord = WorldToVisibleArea(vec3(f2WorldPos, 0.0f), globalLayout.f4VisibleArea);
	float fElevation = texture(elevationSampler, f2ElevTexcoord).x;
	float fHeightFactor = clamp(fElevation / max(globalLayout.fIslandHeight, 0.001f), 0.0f, 1.0f);
	fSpreadDistance *= 1.0f - fHeightFactor * globalLayout.fSpreadHeightDistance;
	float fIntensityHeightFactor = clamp((fElevation - globalLayout.fBaseHeight) / max(globalLayout.fSpreadHeightIntensityTarget - globalLayout.fBaseHeight, 0.001f), 0.0f, 1.0f);
	fDecay *= 1.0f - fIntensityHeightFactor * globalLayout.fSpreadHeightIntensity;

	// World-to-texcoord conversion: texcoord 0-1 covers the lighting area
	float fAspectRatioX = 1.0f / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x);
	float fAspectRatioY = 1.0f / (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w);

	f4OutRed = vec4(0.0f);
	f4OutGreen = vec4(0.0f);
	f4OutBlue = vec4(0.0f);

	// Configurable direction count: evenly spaced unit vectors around the circle
	// Offset by half-step (0.25 * step) to avoid sampling exactly on cardinal axes
	const uint32_t uiRingCount = uint32_t(fRingCountF);

	// Constant per-sample weight (not per-ring falloff): softens outer ring contributions relative to
	// inner rings within a single spread, cancelled out in the final normalization for brightness invariance
	float fInvSqrtDistance = inversesqrt(float(1 + uiRingCount));
	float fTotalSamples = 0.0f;

	for (uint32_t j = 0; j < uiRingCount; ++j)
	{
		// Per-ring jitter: rotation angle scaled by interpolated jitter
		float fRingJitter = globalLayout.pfSpreadRingRotations[j] * fJitter;
		float fRingFalloff = 1.0f - fDistanceFalloff * (float(j + 1) / float(uiRingCount));

		float fDirectionCount = fDirectionCountBase + float(j);
		const uint32_t uiDirectionCount = uint32_t(fDirectionCount);
		fDirectionCount = float(uiDirectionCount);
		const float fDirectionStep = (2.0f * fPi) / fDirectionCount;
		fTotalSamples += fDirectionCount;
		for (uint32_t i = 0; i < uiDirectionCount; ++i)
		{
			float fAngle = float(i) * fDirectionStep + fRingJitter;
			vec2 f2Direction = vec2(cos(fAngle), sin(fAngle));

			// Per-channel EWNS directional weights for this sampling direction
			// Direction is in texcoord space: +X = east (pixel right), +Y = south (pixel down)
			// EWNS channels: R=East(+X), G=West(-X), B=North(-Y pixel), A=South(+Y pixel)
			// Weight = alignment of the sampling direction with each channel's axis
			vec4 f4DirWeight = max(vec4(-f2Direction.x, f2Direction.x, -f2Direction.y, f2Direction.y), 0.0f);

			// Ring distance: total reach = fSpreadDistance, more rings = denser sampling (not wider spread)
			vec2 f2Coord = f2InTexcoord + vec2(fAspectRatioX, fAspectRatioY) * (fSpreadDistance * (float(j + 1) / float(uiRingCount)) * f2Direction);

			vec4 f4Red = texture(redSampler, f2Coord);
			vec4 f4Green = texture(greenSampler, f2Coord);
			vec4 f4Blue = texture(blueSampler, f2Coord);

			f4OutRed += mix(f4Red, f4DirWeight * f4Red, fDirectionality) * fInvSqrtDistance * fRingFalloff;
			f4OutGreen += mix(f4Green, f4DirWeight * f4Green, fDirectionality) * fInvSqrtDistance * fRingFalloff;
			f4OutBlue += mix(f4Blue, f4DirWeight * f4Blue, fDirectionality) * fInvSqrtDistance * fRingFalloff;
		}
	}

	float fNorm = fTotalSamples * fInvSqrtDistance * mix(1.0f, 0.32f, fDirectionality);
	f4OutRed /= fNorm;
	f4OutGreen /= fNorm;
	f4OutBlue /= fNorm;

	f4OutRed *= fDecay;
	f4OutGreen *= fDecay;
	f4OutBlue *= fDecay;

	// Accumulate: add source pixel value to carry forward through passes (skip first pass — deposit already sampled)
	if (fPassIndex > 0.0f)
	{
		f4OutRed += fAccumulationDecay * texture(redSampler, f2InTexcoord);
		f4OutGreen += fAccumulationDecay * texture(greenSampler, f2InTexcoord);
		f4OutBlue += fAccumulationDecay * texture(blueSampler, f2InTexcoord);
	}
}
