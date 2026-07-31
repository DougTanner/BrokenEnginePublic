#version 460

#include "ShaderLayouts.h"
#include "ShaderFunctions.h"
#include "ShaderRandom.h"

// Push constants
layout(push_constant) uniform pushConstants
{
	PushConstantsLayout pushConstantsLayout;
};

// Uniforms
layout (set = 0, binding = kiGlobalBindingGlobalUniform) uniform globalUniform
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
layout (location = 3) out vec4 f4OutRedSpread;
layout (location = 4) out vec4 f4OutGreenSpread;
layout (location = 5) out vec4 f4OutBlueSpread;

// Hue-preserving per-EWNS Reinhard compressor: maps each EWNS channel's RGB average above fThreshold and
// scales all three channels by the resulting ratio. Identity when fCompress == 0. Shared by the pass-0
// deposit compress and the per-pass output compress below.
void CompressHuePreserving(inout vec4 f4Red, inout vec4 f4Green, inout vec4 f4Blue, float fThreshold, float fCompress)
{
	for (int k = 0; k < 4; ++k)
	{
		float fAvg = (f4Red[k] + f4Green[k] + f4Blue[k]) / 3.0f;
		float fExcess = max(fAvg - fThreshold, 0.0f);
		float fMapped = fThreshold + fExcess / (1.0f + fExcess * fCompress);
		float fRatio = fMapped / max(fAvg, 1e-4f);
		f4Red[k]   *= fRatio;
		f4Green[k] *= fRatio;
		f4Blue[k]  *= fRatio;
	}
}

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
	float fSampleJitterRange = mix(globalLayout.fSpreadSampleJitterRangeStart, globalLayout.fSpreadSampleJitterRangeEnd, fT);
	float fSampleJitterClustering = mix(globalLayout.fSpreadSampleJitterClusteringStart, globalLayout.fSpreadSampleJitterClusteringEnd, fT);
	float fDecay = mix(globalLayout.fSpreadDecayStart, globalLayout.fSpreadDecayEnd, fT);
	float fAccumulationDecay = mix(globalLayout.fSpreadAccumulationDecayStart, globalLayout.fSpreadAccumulationDecayEnd, fT);
	float fDistanceFalloff = mix(globalLayout.fSpreadDistanceFalloffStart, globalLayout.fSpreadDistanceFalloffEnd, fT);
	float fOutputThreshold = mix(globalLayout.fSpreadOutputThresholdStart, globalLayout.fSpreadOutputThresholdEnd, fT);
	float fOutputCompress = mix(globalLayout.fSpreadOutputCompressStart, globalLayout.fSpreadOutputCompressEnd, fT);

	// World position of this spread texel and its on-screen (visible-area) UV for the height-fade elevation sample.
	vec2 f2WorldPos = VisibleAreaToWorld(f2InTexcoord, globalLayout.f4LightingArea);
	vec2 f2ElevTexcoord = WorldToVisibleArea(vec3(f2WorldPos, 0.0f), globalLayout.f4VisibleArea);

	// Height fade: attenuate spread above base height
	float fElevation = texture(elevationSampler, f2ElevTexcoord).x;
	float fHeightT = clamp((fElevation - globalLayout.fBaseHeight) * globalLayout.fSpreadHeightEndHeightInv, 0.0f, 1.0f);
	float fHeightFade = pow(fHeightT, globalLayout.fSpreadHeightPower) * globalLayout.fSpreadHeightMultiplier;
	fSpreadDistance *= 1.0f - fHeightFade;
	fDecay *= 1.0f - fHeightFade;

	// World-to-texcoord conversion: texcoord 0-1 covers the lighting area
	float fAspectRatioX = globalLayout.f2LightingAreaExtentInv.x;
	float fAspectRatioY = globalLayout.f2LightingAreaExtentInv.y;

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

	// Per-fragment PRNG: hash run-unique seed with pixel coords, advance per (ring, direction) sample for x/y jitter.
	uvec2 u2Pixel = uvec2(gl_FragCoord.xy);
	uint uiSeed = globalLayout.uiRandomSeed ^ (u2Pixel.x * 0x27d4eb2du) ^ (u2Pixel.y * 0x165667b1u);
	RandomEngine prng = SeedRandomEngine(uiSeed);

	for (uint32_t j = 0; j < uiRingCount; ++j)
	{
		// Per-ring jitter: rotation angle scaled by interpolated jitter
		float fRingJitter = globalLayout.pfSpreadRingRotations[j] * fJitter;
		float fRingFalloff = 1.0f - fDistanceFalloff * (float(j + 1) / float(uiRingCount));

		float fDirectionCount = fDirectionCountBase + float(j);
		const uint32_t uiDirectionCount = uint32_t(fDirectionCount);
		fDirectionCount = float(uiDirectionCount);
		const float fDirectionStep = (2.0f * kfPi) / fDirectionCount;
		fTotalSamples += fDirectionCount * fRingFalloff;

		// Incremental rotation recurrence: advance f2Direction by a fixed per-step rotation each iteration
		// instead of recomputing cos/sin. Re-seeded per ring at fRingJitter to bound accumulated float drift.
		vec2 f2Direction = vec2(cos(fRingJitter), sin(fRingJitter));
		float fCosStep = cos(fDirectionStep);
		float fSinStep = sin(fDirectionStep);
		for (uint32_t i = 0; i < uiDirectionCount; ++i)
		{
			// Per-channel EWNS directional weights for this sampling direction
			// Direction is in texcoord space: +X = east (pixel right), +Y = south (pixel down)
			// EWNS channels: R=East(+X), G=West(-X), B=North(-Y pixel), A=South(+Y pixel)
			// Weight = alignment of the sampling direction with each channel's axis
			vec4 f4DirWeight = max(vec4(f2Direction.x, -f2Direction.x, -f2Direction.y, f2Direction.y), 0.0f);

			// Per-sample x/y jitter — does NOT touch f2Direction. clustering > 1 clusters near origin, < 1 pushes to rim.
			float fSampleJitterR = pow(Random01(prng), fSampleJitterClustering);
			float fSampleJitterAngle = Random01(prng) * (2.0f * kfPi);
			vec2 f2SampleJitterOff = fSampleJitterR * vec2(cos(fSampleJitterAngle), sin(fSampleJitterAngle)) * vec2(fAspectRatioX, fAspectRatioY) * fSampleJitterRange;

			// Ring distance: total reach = fSpreadDistance, more rings = denser sampling (not wider spread)
			vec2 f2Coord = f2InTexcoord + vec2(fAspectRatioX, fAspectRatioY) * (fSpreadDistance * (float(j + 1) / float(uiRingCount)) * f2Direction) + f2SampleJitterOff;

			vec4 f4Red = texture(redSampler, f2Coord);
			vec4 f4Green = texture(greenSampler, f2Coord);
			vec4 f4Blue = texture(blueSampler, f2Coord);

			// Pass 0 reads the deposit texture; Reinhard compress above threshold, hue-preserving per-EWNS
			if (fPassIndex < 0.5f)
			{
				CompressHuePreserving(f4Red, f4Green, f4Blue, globalLayout.fLightingDepositThreshold, globalLayout.fLightingDepositCompress);
			}

			f4OutRed += mix(f4Red, f4DirWeight * f4Red, fDirectionality) * fInvSqrtDistance * fRingFalloff;
			f4OutGreen += mix(f4Green, f4DirWeight * f4Green, fDirectionality) * fInvSqrtDistance * fRingFalloff;
			f4OutBlue += mix(f4Blue, f4DirWeight * f4Blue, fDirectionality) * fInvSqrtDistance * fRingFalloff;

			// Advance to the next evenly-spaced direction by rotating f2Direction by fDirectionStep
			f2Direction = vec2(f2Direction.x * fCosStep - f2Direction.y * fSinStep,
			                   f2Direction.x * fSinStep + f2Direction.y * fCosStep);
		}
	}

	float fNorm = fTotalSamples * fInvSqrtDistance * mix(1.0f, 0.32f, fDirectionality);
	f4OutRed /= fNorm;
	f4OutGreen /= fNorm;
	f4OutBlue /= fNorm;

	f4OutRed *= fDecay;
	f4OutGreen *= fDecay;
	f4OutBlue *= fDecay;

	// Per-pass output Reinhard compressor: hue-preserving per-EWNS. Identity when fOutputCompress == 0.
	CompressHuePreserving(f4OutRed, f4OutGreen, f4OutBlue, fOutputThreshold, fOutputCompress);

	// Spread-only outputs: pre-accumulation snapshot, weighted by the per-pass curve, read by LightCombine
	float fCurveWeight = globalLayout.pfCombineCurvePoints[uint(fPassIndex)];
	f4OutRedSpread = f4OutRed * fCurveWeight;
	f4OutGreenSpread = f4OutGreen * fCurveWeight;
	f4OutBlueSpread = f4OutBlue * fCurveWeight;

	// Accumulate: add source pixel value to carry forward through passes (skip first pass — deposit already sampled)
	if (fPassIndex > 0.0f)
	{
		f4OutRed += fAccumulationDecay * texture(redSampler, f2InTexcoord);
		f4OutGreen += fAccumulationDecay * texture(greenSampler, f2InTexcoord);
		f4OutBlue += fAccumulationDecay * texture(blueSampler, f2InTexcoord);
	}
}
