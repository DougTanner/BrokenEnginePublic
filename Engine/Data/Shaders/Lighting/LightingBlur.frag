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
	float fAspectRatioY = 1.0f / (globalLayout.f4LightingArea.z - globalLayout.f4LightingArea.x);
	float fAspectRatioX = fAspectRatioY;

	const float fDirectionality = globalLayout.fLightingBlurDirectionality;
	const float fBlurDistance = globalLayout.fLightingBlurDistance;
	const float fJitterX = globalLayout.fLightingBlurJitter / pushConstantsLayout.f4Pipeline.x;
	const float fJitterY = globalLayout.fLightingBlurJitter / pushConstantsLayout.f4Pipeline.y;

	// X is east, Y is west, Z is north, W is south
	f4OutRed = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	f4OutGreen = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	f4OutBlue = vec4(0.0f, 0.0f, 0.0f, 0.0f);

	const uint32_t uiDirectionCount = 20;
	const float fDirectionCount = 20.0f;
	const bool bJitter = pushConstantsLayout.f4Pipeline.w > 0.0f;
	const uint32_t uiDistanceCount = uint32_t(abs(pushConstantsLayout.f4Pipeline.w));

	// Pre-computed unit vectors: angle = (i + 0.25) * 2*pi/20
	const vec2 kDirections[20] = vec2[20](
		vec2( 0.98769, 0.15643),  // i=0:   4.5 deg
		vec2( 0.89101, 0.45399),  // i=1:  22.5 deg
		vec2( 0.70711, 0.70711),  // i=2:  40.5 deg
		vec2( 0.45399, 0.89101),  // i=3:  58.5 deg
		vec2( 0.15643, 0.98769),  // i=4:  76.5 deg
		vec2(-0.15643, 0.98769),  // i=5:  94.5 deg
		vec2(-0.45399, 0.89101),  // i=6: 112.5 deg
		vec2(-0.70711, 0.70711),  // i=7: 130.5 deg
		vec2(-0.89101, 0.45399),  // i=8: 148.5 deg
		vec2(-0.98769, 0.15643),  // i=9: 166.5 deg
		vec2(-0.98769,-0.15643),  // i=10: 184.5 deg
		vec2(-0.89101,-0.45399),  // i=11: 202.5 deg
		vec2(-0.70711,-0.70711),  // i=12: 220.5 deg
		vec2(-0.45399,-0.89101),  // i=13: 238.5 deg
		vec2(-0.15643,-0.98769),  // i=14: 256.5 deg
		vec2( 0.15643,-0.98769),  // i=15: 274.5 deg
		vec2( 0.45399,-0.89101),  // i=16: 292.5 deg
		vec2( 0.70711,-0.70711),  // i=17: 310.5 deg
		vec2( 0.89101,-0.45399),  // i=18: 328.5 deg
		vec2( 0.98769,-0.15643)   // i=19: 346.5 deg
	);

	const float kfDivisor = 1.0f / 4294967295.0f;
	uint32_t uiW = uint32_t(f2InTexcoord.x * 4294967295.0f);
	uint32_t uiZ = uint32_t(f2InTexcoord.y * 4294967295.0f);

	// Constant weight factor matching original behavior (always 5.0, independent of ring count)
	const float fDistance = 5.0f;
	const float fInvSqrtDistance = inversesqrt(fDistance);

	for (uint32_t i = 0; i < uiDirectionCount; ++i)
	{
		vec2 f2Direction = kDirections[i];
		vec4 f4DirWeight = max(vec4(-f2Direction.x, f2Direction.x, f2Direction.y, -f2Direction.y), 0.0f);

		for (uint32_t j = 0; j < uiDistanceCount; ++j)
		{
			vec2 f2Jitter = vec2(0.0f);
			if (bJitter)
			{
				uiW = 18000 * (uiW & 65535) + (uiW >> 16);
				uiZ = 36969 * (uiZ & 65535) + (uiZ >> 16);
				float fRandomOne = fDistance * fJitterX * (-1.0f + 2.0f * float((uiZ << 16) + uiW) * kfDivisor);
				uiW = 18000 * (uiW & 65535) + (uiW >> 16);
				uiZ = 36969 * (uiZ & 65535) + (uiZ >> 16);
				float fRandomTwo = fDistance * fJitterY * (-1.0f + 2.0f * float((uiZ << 16) + uiW) * kfDivisor);
				f2Jitter = vec2(fRandomOne, fRandomTwo);
			}

			vec2 f2Coord = f2InTexcoord + f2Jitter + vec2(fAspectRatioX, fAspectRatioY) * (fBlurDistance * float(j + 1) * f2Direction);

			vec4 f4Red = texture(redSampler, f2Coord);
			vec4 f4Green = texture(greenSampler, f2Coord);
			vec4 f4Blue = texture(blueSampler, f2Coord);

			f4OutRed += mix(f4Red, f4DirWeight * f4Red, fDirectionality) * fInvSqrtDistance;
			f4OutGreen += mix(f4Green, f4DirWeight * f4Green, fDirectionality) * fInvSqrtDistance;
			f4OutBlue += mix(f4Blue, f4DirWeight * f4Blue, fDirectionality) * fInvSqrtDistance;
		}
	}

	float fNorm = fDirectionCount * float(uiDistanceCount) * mix(1.0f, 0.32f, fDirectionality);
	f4OutRed /= fNorm * pushConstantsLayout.f4Pipeline.z;
	f4OutGreen /= fNorm * pushConstantsLayout.f4Pipeline.z;
	f4OutBlue /= fNorm * pushConstantsLayout.f4Pipeline.z;
}
