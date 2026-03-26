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

// Input
layout (location = 0) in flat int iInInstanceIndex;
layout (location = 1) in vec2 f2InTexcoord;

// Output
layout (location = 0) out vec4 f4OutRed;
layout (location = 1) out vec4 f4OutGreen;
layout (location = 2) out vec4 f4OutBlue;

void main()
{
	// World-to-texcoord conversion: texcoord 0-1 covers the lighting area
	float fAspectRatioX = 1.0f / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x);
	float fAspectRatioY = 1.0f / (globalLayout.f4LightingArea.y - globalLayout.f4LightingArea.w);

	const float fDirectionality = globalLayout.fSpreadDirectionality;
	const float fSpreadDistance = globalLayout.fSpreadDistance;

	f4OutRed = vec4(0.0f);
	f4OutGreen = vec4(0.0f);
	f4OutBlue = vec4(0.0f);

	// Configurable direction count: evenly spaced unit vectors around the circle
	// Offset by half-step (0.25 * step) to avoid sampling exactly on cardinal axes
	const uint32_t uiRingCount = uint32_t(globalLayout.fSpreadRingCount);

	// Constant normalization factor (not per-ring falloff): produces an implicit 1/sqrt(1+N) brightness
	// scaling that softens the result as ring count increases, compensated by the fSpreadDecay slider
	float fInvSqrtDistance = inversesqrt(float(1 + uiRingCount));
	float fTotalSamples = 0.0f;

	for (uint32_t j = 0; j < uiRingCount; ++j)
	{
		// Per-ring jitter: alternating perturbation breaks even spacing so the sum changes
		// Even directions shift clockwise, odd shift counter-clockwise by the ring's jitter amount
		float fRingJitter = globalLayout.pfSpreadRingRotations[j];

		float fDirectionCount = globalLayout.fSpreadDirectionCount + float(j);
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

			f4OutRed += mix(f4Red, f4DirWeight * f4Red, fDirectionality) * fInvSqrtDistance;
			f4OutGreen += mix(f4Green, f4DirWeight * f4Green, fDirectionality) * fInvSqrtDistance;
			f4OutBlue += mix(f4Blue, f4DirWeight * f4Blue, fDirectionality) * fInvSqrtDistance;
		}
	}

	float fNorm = fTotalSamples * mix(1.0f, 0.32f, fDirectionality);
	f4OutRed /= fNorm;
	f4OutGreen /= fNorm;
	f4OutBlue /= fNorm;

	f4OutRed *= globalLayout.fSpreadDecay;
	f4OutGreen *= globalLayout.fSpreadDecay;
	f4OutBlue *= globalLayout.fSpreadDecay;
}
